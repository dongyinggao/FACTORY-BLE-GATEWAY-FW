#include "csv_logger.h"

#include <stdio.h>
#include <string.h>
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
static const char *CSV_HEADER =
    "timestamp,time_synced,event_uptime_s,broadcast_started_uptime_s,last_seen_uptime_s,"
    "end_detected_uptime_s,gateway_id,gateway_location,device_id,device_name,mac,address_type,"
    "event,broadcast_started_at,last_seen_at,end_detected_at,rssi,scanner_state";
static bool sd_ready;

static void csv_write_event(const device_lifecycle_event_t *event)
{
    const gateway_config_t *config = gateway_config_get();
    char line[CSV_LIFECYCLE_LINE_MAX_LEN];
    char path[96];
    char backup_path[96];
    char date_name[9];
    char header[256];
    time_t now;
    struct tm local_time;
    csv_lifecycle_event_t csv_event = {
        .type = event->type,
        .address_type = event->address_type,
        .rssi = event->rssi,
        .broadcast_started_ms = event->broadcast_started_ms,
        .last_seen_ms = event->last_seen_ms,
        .end_detected_ms = event->end_detected_ms,
        .broadcast_started_wall_ms = event->broadcast_started_wall_ms,
        .last_seen_wall_ms = event->last_seen_wall_ms,
        .end_detected_wall_ms = event->end_detected_wall_ms,
    };
    FILE *file;

    if (!sd_ready) {
        return;
    }
    memcpy(csv_event.name, event->name, sizeof(csv_event.name));
    memcpy(csv_event.address, event->address, sizeof(csv_event.address));
    now = time(NULL);
    localtime_r(&now, &local_time);
    if (local_time.tm_year + 1900 < 2024) {
        snprintf(date_name, sizeof(date_name), "00000000");
    } else if (strftime(date_name, sizeof(date_name), "%Y%m%d", &local_time) == 0) {
        snprintf(date_name, sizeof(date_name), "00000000");
    }
    snprintf(path, sizeof(path), BSP_SD_MOUNT_POINT "/data/%s.csv", date_name);
    file = fopen(path, "a+");

    if (file == NULL) {
        ESP_LOGE(TAG, "unable to open CSV file");
        sd_ready = false;
        return;
    }
    if (ftell(file) == 0) {
        fprintf(file, "%s\n", CSV_HEADER);
    } else {
        rewind(file);
        if (fgets(header, sizeof(header), file) == NULL ||
            strncmp(header, CSV_HEADER, strlen(CSV_HEADER)) != 0 ||
            (header[strlen(CSV_HEADER)] != '\n' && header[strlen(CSV_HEADER)] != '\r')) {
            fclose(file);
            snprintf(backup_path, sizeof(backup_path), BSP_SD_MOUNT_POINT "/data/%s.OLD", date_name);
            if (rename(path, backup_path) != 0) {
                ESP_LOGE(TAG, "CSV schema mismatch; unable to preserve %s", path);
                sd_ready = false;
                return;
            }
            file = fopen(path, "w");
            if (file == NULL) {
                ESP_LOGE(TAG, "unable to create CSV file after schema migration");
                sd_ready = false;
                return;
            }
            fprintf(file, "%s\n", CSV_HEADER);
            ESP_LOGW(TAG, "CSV schema changed; previous file saved as %s", backup_path);
        }
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
    device_lifecycle_event_t event;
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
