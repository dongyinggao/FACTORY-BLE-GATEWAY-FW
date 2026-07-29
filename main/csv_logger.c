#include "csv_logger.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "bsp/m5stack_core_s3.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "device_manager.h"
#include "csv_formatter.h"
#include "gateway_config.h"

static const char *TAG = "csv_logger";
static bool sd_ready;

static void csv_write_event(const device_manager_event_t *event)
{
    const gateway_config_t *config = gateway_config_get();
    char line[CSV_LIFECYCLE_LINE_MAX_LEN];
    char path[96];
    time_t now;
    struct tm local_time;
    csv_lifecycle_event_t csv_event = {
        .type = event->type == DEVICE_MANAGER_EVENT_BROADCAST_STARTED ?
                    CSV_LIFECYCLE_BROADCAST_STARTED : CSV_LIFECYCLE_BROADCAST_ENDED,
        .device = event->device,
    };
    FILE *file;

    if (!sd_ready) {
        return;
    }
    now = time(NULL);
    localtime_r(&now, &local_time);
    if (local_time.tm_year + 1900 < 2024) {
        snprintf(path, sizeof(path), BSP_SD_MOUNT_POINT "/data/00000000.csv");
    } else {
        snprintf(path, sizeof(path), BSP_SD_MOUNT_POINT "/data/%04d%02d%02d.csv",
                 local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday);
    }
    file = fopen(path, "a+");

    if (file == NULL) {
        ESP_LOGE(TAG, "unable to open CSV file");
        sd_ready = false;
        return;
    }
    if (ftell(file) == 0) {
        fputs("timestamp,time_synced,event_uptime_s,gateway_id,gateway_location,device_id,device_name,mac,address_type,event,broadcast_started_at,last_seen_at,end_detected_at,rssi,scanner_state\n", file);
    }
    if (csv_format_lifecycle_event(line, sizeof(line), &csv_event, config) < 0) {
        ESP_LOGE(TAG, "CSV record formatting failed");
        fclose(file);
        return;
    }
    fputs(line, file);
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) {
        ESP_LOGE(TAG, "CSV sync failed");
        sd_ready = false;
    }
    fclose(file);
}

static void csv_logger_task(void *parameter)
{
    device_manager_event_t event;
    QueueHandle_t queue = device_manager_get_capture_queue();

    (void)parameter;
    while (true) {
        if (xQueueReceive(queue, &event, portMAX_DELAY) == pdTRUE) {
            csv_write_event(&event);
        }
    }
}

void csv_logger_start(void)
{
    esp_err_t result = bsp_sdcard_mount();

    if (result != ESP_OK) {
        ESP_LOGW(TAG, "SD card unavailable: %s", esp_err_to_name(result));
        sd_ready = false;
    } else {
        mkdir(BSP_SD_MOUNT_POINT "/data", 0775);
        sd_ready = true;
        ESP_LOGI(TAG, "SD CSV logging ready at %s/data", BSP_SD_MOUNT_POINT);
    }
    xTaskCreate(csv_logger_task, "csv_logger", 4096, NULL, 4, NULL);
}

bool csv_logger_is_ready(void)
{
    return sd_ready;
}
