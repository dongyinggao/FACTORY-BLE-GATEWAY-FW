#include "storage_manager.h"

#include <errno.h>
#include <sys/stat.h>

#include "bsp/m5stack_core_s3.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "storage_state_core.h"
#include "device_manager.h"

#define STORAGE_RETRY_DELAY_S 5U

static const char *TAG = "storage_manager";
static SemaphoreHandle_t storage_mutex;
static storage_state_core_t storage_state;
static esp_err_t storage_last_error = ESP_FAIL;

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
}

/*
 * CoreS3 shares SPI3 between the LCD and SD card.  The BSP convenience
 * unmount routine also frees SPI3, which aborts while the LCD still owns a
 * device on that bus.  Detach only the FATFS/SD device here; keep the shared
 * SPI bus alive for the display and let bsp_sdcard_mount() attach a new card.
 */
static esp_err_t unmount_sd_preserving_shared_spi(void)
{
    sdmmc_card_t *card = bsp_sdcard_get_handle();

    if (card == NULL) {
        return ESP_OK;
    }
    return esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, card);
}

static void try_mount_locked(void)
{
    esp_err_t result = bsp_sdcard_mount();

    if (result == ESP_OK && mkdir(BSP_SD_MOUNT_POINT "/data", 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "cannot create SD data directory");
        (void)unmount_sd_preserving_shared_spi();
        result = ESP_FAIL;
    }
    storage_state_core_mount_result(&storage_state, result == ESP_OK, uptime_s(),
                                    STORAGE_RETRY_DELAY_S);
    storage_last_error = result;
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "SD mounted at %s (generation %lu)", BSP_SD_MOUNT_POINT,
                 (unsigned long)storage_state.generation);
    } else {
        ESP_LOGW(TAG, "SD mount failed: %s; retry in %u s", esp_err_to_name(result),
                 STORAGE_RETRY_DELAY_S);
    }
    device_manager_request_ui_status_refresh();
}

static void storage_manager_task(void *parameter)
{
    (void)parameter;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (xSemaphoreTake(storage_mutex, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!storage_state.ready && !storage_state.full && storage_state.mounted) {
            esp_err_t result = unmount_sd_preserving_shared_spi();
            storage_state.mounted = false;
            if (result == ESP_OK) {
                ESP_LOGW(TAG, "SD unmounted after reported I/O failure");
            } else {
                ESP_LOGW(TAG, "SD unmount after I/O failure also failed: %s",
                         esp_err_to_name(result));
            }
            device_manager_request_ui_status_refresh();
        }
        if (storage_state_core_retry_due(&storage_state, uptime_s())) {
            try_mount_locked();
        }
        xSemaphoreGive(storage_mutex);
    }
}

void storage_manager_start(void)
{
    if (storage_mutex != NULL) {
        return;
    }
    storage_mutex = xSemaphoreCreateMutex();
    configASSERT(storage_mutex != NULL);
    storage_state_core_init(&storage_state);
    xSemaphoreTake(storage_mutex, portMAX_DELAY);
    try_mount_locked();
    xSemaphoreGive(storage_mutex);
    xTaskCreate(storage_manager_task, "storage_mgr", 4096, NULL, 4, NULL);
}

bool storage_manager_lock(void)
{
    if (storage_mutex == NULL || xSemaphoreTake(storage_mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (!storage_state.mounted) {
        xSemaphoreGive(storage_mutex);
        return false;
    }
    return true;
}

void storage_manager_unlock(void)
{
    if (storage_mutex != NULL) {
        xSemaphoreGive(storage_mutex);
    }
}

void storage_manager_report_io_failure(int error_code)
{
    storage_state_core_mark_failed(&storage_state, uptime_s(), STORAGE_RETRY_DELAY_S);
    storage_last_error = error_code == 0 ? ESP_FAIL : error_code;
    device_manager_request_ui_status_refresh();
}

void storage_manager_report_full(int error_code)
{
    storage_state_core_mark_full(&storage_state);
    storage_last_error = error_code == 0 ? ENOSPC : error_code;
    device_manager_request_ui_status_refresh();
}

void storage_manager_report_write_success(void)
{
    storage_state_core_mark_write_success(&storage_state);
    storage_last_error = ESP_OK;
}

bool storage_manager_is_ready(void)
{
    return storage_state.ready;
}

bool storage_manager_is_full(void)
{
    return storage_state.full;
}

uint32_t storage_manager_generation(void)
{
    return storage_state.generation;
}

const char *storage_manager_status_text(void)
{
    return storage_state.ready ? "OK" : (storage_state.full ? "Full" : "Retry");
}

esp_err_t storage_manager_last_error(void)
{
    return storage_state.ready ? ESP_OK : storage_last_error;
}
