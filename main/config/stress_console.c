#include "stress_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_scanner.h"
#include "device_manager.h"

#define STRESS_MAX_DEVICES 128U
#define STRESS_DEFAULT_DEVICES 128U
#define STRESS_DEFAULT_RATE_PER_SECOND 20U
#define STRESS_MAX_RATE_PER_SECOND 50U

static const char *TAG = "stress";

#if CONFIG_BLE_GATEWAY_STRESS_TEST_ENABLED
typedef struct {
    TaskHandle_t task;
    volatile bool stop_requested;
    volatile uint16_t requested;
    volatile uint16_t submitted;
    volatile uint16_t rejected;
    volatile uint16_t rate_per_second;
} stress_run_t;

static stress_run_t stress_run;

static bool parse_range(const char *text, unsigned int minimum, unsigned int maximum,
                        unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);
    if (text[0] == '\0' || end == NULL || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        return false;
    }
    *value = (unsigned int)parsed;
    return true;
}

static ble_scan_report_t stress_report_for(uint16_t index)
{
    ble_scan_report_t report = {0};

    snprintf(report.name, sizeof(report.name), "%s%u",
             (index & 1U) == 0U ? "SM_ICM9" : "SM_ICD9", (unsigned int)(index + 1U));
    report.address[0] = 0x02U;
    report.address[1] = 0x53U;
    report.address[2] = 0x54U;
    report.address[3] = 0x52U;
    report.address[4] = (uint8_t)(index >> 8U);
    report.address[5] = (uint8_t)index;
    report.address_type = 1U;
    report.rssi = (int8_t)(-35 - (index % 55U));
    return report;
}

static void stress_task(void *parameter)
{
    TickType_t interval;

    (void)parameter;
    interval = pdMS_TO_TICKS(1000U / stress_run.rate_per_second);
    if (interval == 0U) {
        interval = 1U;
    }

    for (uint16_t index = 0; index < stress_run.requested && !stress_run.stop_requested; ++index) {
        ble_scan_report_t report = stress_report_for(index);
        if (ble_scanner_submit_diagnostic_report(&report)) {
            ++stress_run.submitted;
        } else {
            ++stress_run.rejected;
        }
        vTaskDelay(interval);
    }

    ESP_LOGI(TAG, "run complete: submitted=%u rejected=%u; lifecycle endings follow bcast_end_s",
             (unsigned int)stress_run.submitted, (unsigned int)stress_run.rejected);
    stress_run.task = NULL;
    vTaskDelete(NULL);
}

static void print_stress_status(void)
{
    printf("stress=%s requested=%u submitted=%u rejected=%u rate=%u/s\n",
           stress_run.task == NULL ? "idle" : "running",
           (unsigned int)stress_run.requested, (unsigned int)stress_run.submitted,
           (unsigned int)stress_run.rejected, (unsigned int)stress_run.rate_per_second);
    printf("queue_high_water=scan:%lu[%u] ui:%lu[%u] capture:%lu[%u] upload:%lu[%u]\n",
           (unsigned long)ble_scanner_event_queue_high_watermark(),
           (unsigned int)BLE_EVENT_QUEUE_MAX_LEN,
           (unsigned long)device_manager_ui_queue_high_watermark(),
           (unsigned int)DEVICE_MANAGER_UI_QUEUE_LEN,
           (unsigned long)device_manager_capture_queue_high_watermark(),
           (unsigned int)DEVICE_MANAGER_LIFECYCLE_QUEUE_LEN,
           (unsigned long)device_manager_upload_queue_high_watermark(),
           (unsigned int)DEVICE_MANAGER_LIFECYCLE_QUEUE_LEN);
}

static int stress_command(int argc, char **argv)
{
    unsigned int count = STRESS_DEFAULT_DEVICES;
    unsigned int rate = STRESS_DEFAULT_RATE_PER_SECOND;

    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        print_stress_status();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        if (stress_run.task != NULL) {
            stress_run.stop_requested = true;
            printf("stress stop requested\n");
        } else {
            printf("stress is idle\n");
        }
        return 0;
    }
    if (argc >= 2 && argc <= 4 && strcmp(argv[1], "run") == 0) {
        uint16_t registered;

        if (stress_run.task != NULL) {
            printf("stress is already running\n");
            return 1;
        }
        if (argc >= 3 && !parse_range(argv[2], 1U, STRESS_MAX_DEVICES, &count)) {
            printf("device count must be 1..%u\n", STRESS_MAX_DEVICES);
            return 1;
        }
        if (argc == 4 && !parse_range(argv[3], 1U, STRESS_MAX_RATE_PER_SECOND, &rate)) {
            printf("rate must be 1..%u reports/s\n", STRESS_MAX_RATE_PER_SECOND);
            return 1;
        }
        if (!ble_scanner_is_enabled() || ble_scanner_get_state() != BLE_SCANNER_STATE_SCANNING) {
            printf("scanner is not actively scanning; start scanning before running stress\n");
            return 1;
        }
        registered = device_manager_registered_count();
        if (registered >= STRESS_MAX_DEVICES || count > STRESS_MAX_DEVICES - registered) {
            printf("not enough device-table capacity: registered=%u requested=%u max=%u\n",
                   (unsigned int)registered, count, STRESS_MAX_DEVICES);
            return 1;
        }

        memset(&stress_run, 0, sizeof(stress_run));
        stress_run.requested = (uint16_t)count;
        stress_run.rate_per_second = (uint16_t)rate;
        if (xTaskCreate(stress_task, "stress_inject", 3072, NULL, 1, &stress_run.task) != pdPASS) {
            stress_run.task = NULL;
            printf("unable to create stress task\n");
            return 1;
        }
        printf("stress started: %u synthetic devices at %u reports/s\n", count, rate);
        return 0;
    }

    printf("usage: stress run [1..128] [1..50 reports/s] | stress status | stress stop\n");
    return 1;
}
#endif

void stress_console_register(void)
{
#if CONFIG_BLE_GATEWAY_STRESS_TEST_ENABLED
    const esp_console_cmd_t command = {
        .command = "stress",
        .help = "diagnostic synthetic BLE load: run, status, stop",
        .func = &stress_command,
    };
    (void)esp_console_cmd_register(&command);
#else
    ESP_LOGI(TAG, "stress command disabled in this build");
#endif
}
