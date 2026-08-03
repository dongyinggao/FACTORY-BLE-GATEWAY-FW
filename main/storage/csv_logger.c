#include "csv_logger.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bsp/m5stack_core_s3.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "device_manager.h"
#include "csv_formatter.h"
#include "gateway_config.h"
#include "storage_manager.h"

static const char *TAG = "csv_logger";
static const char *CSV_HEADER =
    "recorded_at,time_synced,event_uptime_s,gateway_id,gateway_location,device_mac,device_name,"
    "event,broadcast_id,broadcast_started_at,broadcast_ended_at,broadcast_duration_s,"
    "end_detected_at,last_rssi";

static void csv_report_write_failure(const char *message)
{
    int error_code = errno;

    ESP_LOGE(TAG, "%s: errno=%d", message, error_code);
    if (error_code == ENOSPC) {
        storage_manager_report_full(error_code);
    } else {
        storage_manager_report_io_failure(error_code);
    }
}

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
    /* csv_lifecycle_event_t is the persisted view of the immutable lifecycle
     * event. Copy the whole structure so future fields (for example
     * broadcast_id) cannot be accidentally omitted during conversion. */
    csv_lifecycle_event_t csv_event = *event;
    FILE *file;

    if (!storage_manager_lock()) {
        return;
    }
    if (!storage_manager_is_ready()) {
        storage_manager_unlock();
        return;
    }
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
        csv_report_write_failure("unable to open CSV file");
        storage_manager_unlock();
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
                csv_report_write_failure("CSV schema mismatch; unable to preserve previous file");
                storage_manager_unlock();
                return;
            }
            file = fopen(path, "w");
            if (file == NULL) {
                csv_report_write_failure("unable to create CSV file after schema migration");
                storage_manager_unlock();
                return;
            }
            fprintf(file, "%s\n", CSV_HEADER);
            ESP_LOGW(TAG, "CSV schema changed; previous file saved as %s", backup_path);
        }
    }
    if (csv_format_lifecycle_event(line, sizeof(line), &csv_event, config) < 0) {
        ESP_LOGE(TAG, "CSV record formatting failed");
        fclose(file);
        storage_manager_unlock();
        return;
    }
    if (fputs(line, file) == EOF || fflush(file) != 0 || fsync(fileno(file)) != 0) {
        csv_report_write_failure("CSV persistence failed");
    } else {
        storage_manager_report_write_success();
    }
    fclose(file);
    storage_manager_unlock();
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
    xTaskCreate(csv_logger_task, "csv_logger", 4096, NULL, 4, NULL);
}

bool csv_logger_is_ready(void)
{
    return storage_manager_is_ready();
}
