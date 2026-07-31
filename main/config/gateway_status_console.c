#include "gateway_status_console.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_timer.h"

#include "ble_scanner.h"
#include "device_manager.h"
#include "gateway_config.h"
#include "gateway_publisher.h"
#include "mqtt_service.h"
#include "network_manager.h"
#include "outbox.h"
#include "storage_manager.h"
#include "system_diagnostics.h"
#include "time_service.h"

#define STATUS_VALUE_WIDTH 54

static const char *scanner_state_text(ble_scanner_state_t state)
{
    switch (state) {
    case BLE_SCANNER_STATE_IDLE:
        return "Idle";
    case BLE_SCANNER_STATE_SCANNING:
        return "Scanning";
    case BLE_SCANNER_STATE_STOPPING:
        return "Stopping";
    case BLE_SCANNER_STATE_ERROR:
        return "Error";
    default:
        return "Unknown";
    }
}

static void print_status_row(const char *name, const char *value)
{
    printf("| %-18.18s | %-*.*s |\n", name, STATUS_VALUE_WIDTH, STATUS_VALUE_WIDTH, value);
}

static void print_status(void)
{
    const gateway_config_t *config = gateway_config_get();
    ble_scan_timing_window_t timing = ble_scanner_last_timing_window();
    char value[96];

    printf("\nGateway status\n");
    printf("+--------------------+--------------------------------------------------------+\n");
    snprintf(value, sizeof(value), "%lu s", (unsigned long)(esp_timer_get_time() / 1000000LL));
    print_status_row("Uptime", value);
    print_status_row("Gateway ID", config->gateway_id[0] ? config->gateway_id : "<unset>");
    print_status_row("Location", config->gateway_location[0] ? config->gateway_location : "<unset>");
    snprintf(value, sizeof(value), "%s (%s)", scanner_state_text(ble_scanner_get_state()),
             ble_scanner_is_enabled() ? "enabled" : "disabled");
    print_status_row("BLE scanner", value);
    snprintf(value, sizeof(value), "%u registered / %u broadcasting / %lu rejected",
             (unsigned int)device_manager_registered_count(),
             (unsigned int)device_manager_broadcasting_count(),
             (unsigned long)device_manager_table_reject_count());
    print_status_row("Devices", value);
    snprintf(value, sizeof(value), "scan=%u, ui=%u, capture=%u, upload=%u",
             (unsigned int)ble_scanner_event_queue_depth(),
             (unsigned int)device_manager_ui_queue_depth(),
             (unsigned int)device_manager_capture_queue_depth(),
             (unsigned int)device_manager_upload_queue_depth());
    print_status_row("Queue depth", value);
    snprintf(value, sizeof(value), "scan=%lu[%u], ui=%lu[%u]",
             (unsigned long)ble_scanner_event_queue_high_watermark(),
             (unsigned int)BLE_EVENT_QUEUE_MAX_LEN,
             (unsigned long)device_manager_ui_queue_high_watermark(),
             (unsigned int)DEVICE_MANAGER_UI_QUEUE_LEN);
    print_status_row("Queue high water", value);
    snprintf(value, sizeof(value), "capture=%lu[%u], upload=%lu[%u]",
             (unsigned long)device_manager_capture_queue_high_watermark(),
             (unsigned int)DEVICE_MANAGER_LIFECYCLE_QUEUE_LEN,
             (unsigned long)device_manager_upload_queue_high_watermark(),
             (unsigned int)DEVICE_MANAGER_LIFECYCLE_QUEUE_LEN);
    print_status_row("Queue high water", value);
    snprintf(value, sizeof(value), "avg=%lu us, max=%lu us",
             (unsigned long)timing.callback_avg_us, (unsigned long)timing.callback_max_us);
    print_status_row("Scan callback 30s", value);
    snprintf(value, sizeof(value), "samples=%lu, avg=%lu us, max=%lu us",
             (unsigned long)timing.queue_wait_samples,
             (unsigned long)timing.queue_wait_avg_us,
             (unsigned long)timing.queue_wait_max_us);
    print_status_row("Scan wait 30s", value);
    snprintf(value, sizeof(value), "scan=%lu, ui=%lu, capture=%lu, upload=%lu",
             (unsigned long)ble_scanner_report_drop_count(),
             (unsigned long)device_manager_ui_drop_count(),
             (unsigned long)device_manager_capture_drop_count(),
             (unsigned long)device_manager_upload_drop_count());
    print_status_row("Dropped events", value);
    snprintf(value, sizeof(value), "volatile=%lu, unrecoverable=%lu",
             (unsigned long)gateway_publisher_volatile_publish_count(),
             (unsigned long)gateway_publisher_unrecoverable_drop_count());
    print_status_row("Delivery loss", value);
    snprintf(value, sizeof(value), "%s (generation %lu, error=%ld)",
             storage_manager_status_text(),
             (unsigned long)storage_manager_generation(),
             (long)storage_manager_last_error());
    print_status_row("SD card", value);
    snprintf(value, sizeof(value), "Wi-Fi=%s, MQTT=%s, SNTP=%s",
             network_manager_status_text(), mqtt_service_status_text(), time_service_status_text());
    print_status_row("Network", value);
    snprintf(value, sizeof(value), "%s, %lu messages, %lu B, historical_failures=%lu",
             gateway_outbox_status_text(), (unsigned long)gateway_outbox_pending_count(),
             (unsigned long)gateway_outbox_pending_bytes(),
             (unsigned long)gateway_outbox_failure_count());
    print_status_row("Outbox", value);
    printf("+--------------------+--------------------------------------------------------+\n");
}

static int system_command(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: sys status | mem | tasks\n");
        return 1;
    }
    if (strcmp(argv[1], "status") == 0) {
        print_status();
        return 0;
    }
    if (strcmp(argv[1], "mem") == 0) {
        system_diagnostics_print_memory();
        return 0;
    }
    if (strcmp(argv[1], "tasks") == 0) {
        if (system_diagnostics_print_tasks()) {
            return 0;
        }
        printf("sys tasks is disabled; enable task diagnostics in menuconfig\n");
        return 0;
    }
    printf("usage: sys status | mem | tasks\n");
    return 1;
}

void gateway_status_console_register(void)
{
    const esp_console_cmd_t system_command_definition = {
        .command = "sys",
        .help = "gateway diagnostics: status, mem, tasks",
        .func = &system_command,
    };

    (void)esp_console_cmd_register(&system_command_definition);
}
