#include "system_console.h"

#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ble_scanner.h"
#include "csv_logger.h"
#include "device_manager.h"
#include "gateway_config.h"
#include "mqtt_service.h"
#include "network_manager.h"
#include "outbox.h"
#include "storage_manager.h"
#include "time_service.h"

#define SYSTEM_CONSOLE_MAX_TASKS 24U
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

static void print_memory_line(const char *name, uint32_t caps)
{
    size_t total = heap_caps_get_total_size(caps);
    size_t free = heap_caps_get_free_size(caps);
    size_t used = total >= free ? total - free : 0U;
    size_t largest = heap_caps_get_largest_free_block(caps);
    unsigned int used_tenths = total == 0U ? 0U : (unsigned int)((used * 1000U) / total);

    printf("| %-14.14s | %10u | %5u.%1u | %10u | %10u | %10u |\n", name,
           (unsigned int)used, used_tenths / 10U, used_tenths % 10U,
           (unsigned int)free, (unsigned int)total, (unsigned int)largest);
}

static void print_status(void)
{
    const gateway_config_t *config = gateway_config_get();
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
    snprintf(value, sizeof(value), "%u registered / %u broadcasting",
             (unsigned int)device_manager_registered_count(),
             (unsigned int)device_manager_broadcasting_count());
    print_status_row("Devices", value);
    snprintf(value, sizeof(value), "scan=%u, ui=%u, capture=%u, upload=%u",
             (unsigned int)ble_scanner_event_queue_depth(),
             (unsigned int)device_manager_ui_queue_depth(),
             (unsigned int)device_manager_capture_queue_depth(),
             (unsigned int)device_manager_upload_queue_depth());
    print_status_row("Queue depth", value);
    snprintf(value, sizeof(value), "scan=%lu, ui=%lu, capture=%lu, upload=%lu",
             (unsigned long)ble_scanner_event_queue_high_watermark(),
             (unsigned long)device_manager_ui_queue_high_watermark(),
             (unsigned long)device_manager_capture_queue_high_watermark(),
             (unsigned long)device_manager_upload_queue_high_watermark());
    print_status_row("Queue high water", value);
    snprintf(value, sizeof(value), "scan=%lu, ui=%lu, capture=%lu, upload=%lu",
             (unsigned long)ble_scanner_report_drop_count(),
             (unsigned long)device_manager_ui_drop_count(),
             (unsigned long)device_manager_capture_drop_count(),
             (unsigned long)device_manager_upload_drop_count());
    print_status_row("Dropped events", value);
    snprintf(value, sizeof(value), "%s (generation %lu, error=%ld)",
             storage_manager_status_text(),
             (unsigned long)storage_manager_generation(),
             (long)storage_manager_last_error());
    print_status_row("SD card", value);
    snprintf(value, sizeof(value), "Wi-Fi=%s, MQTT=%s, SNTP=%s",
             network_manager_status_text(), mqtt_service_status_text(), time_service_status_text());
    print_status_row("Network", value);
    snprintf(value, sizeof(value), "%s, %lu messages, %lu B, failures=%lu",
             gateway_outbox_status_text(), (unsigned long)gateway_outbox_pending_count(),
             (unsigned long)gateway_outbox_pending_bytes(),
             (unsigned long)gateway_outbox_failure_count());
    print_status_row("Outbox", value);
    printf("+--------------------+--------------------------------------------------------+\n");
}

static void print_memory(void)
{
    printf("\nMemory usage\n");
    printf("+----------------+------------+---------+------------+------------+------------+\n");
    printf("| Pool           |   Used [B] |  Used %% |   Free [B] |  Total [B] |Largest [B] |\n");
    printf("+----------------+------------+---------+------------+------------+------------+\n");
    print_memory_line("internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    print_memory_line("DMA internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    print_memory_line("psram", MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    printf("+----------------+------------+---------+------------+------------+------------+\n");
    printf("DMA internal is a capability subset of internal memory.\n");
}

#if CONFIG_BLE_GATEWAY_SYS_TASKS_ENABLED
static const char *task_state_text(eTaskState state)
{
    static const char *const states[] = { "Running", "Ready", "Blocked", "Suspended", "Deleted", "Invalid" };

    return state <= eInvalid ? states[state] : "Unknown";
}

static void print_tasks(void)
{
    static TaskStatus_t tasks[SYSTEM_CONSOLE_MAX_TASKS];
    configRUN_TIME_COUNTER_TYPE total_runtime = 0;
    UBaseType_t count = uxTaskGetSystemState(tasks, SYSTEM_CONSOLE_MAX_TASKS, &total_runtime);

    printf("\nTask snapshot\n");
    printf("+------------------+------------+----------+---------+------------------+\n");
    printf("| Name             | State      | Priority | CPU %%   | Stack free [B]   |\n");
    printf("+------------------+------------+----------+---------+------------------+\n");
    for (UBaseType_t index = 0; index < count; ++index) {
        unsigned int cpu_tenths = total_runtime == 0U ? 0U :
                                  (unsigned int)(((uint64_t)tasks[index].ulRunTimeCounter * 1000U) /
                                                 total_runtime);

        printf("| %-16.16s | %-10.10s | %8u | %5u.%1u | %16u |\n", tasks[index].pcTaskName,
               task_state_text(tasks[index].eCurrentState),
               (unsigned int)tasks[index].uxCurrentPriority,
               cpu_tenths / 10U, cpu_tenths % 10U,
               (unsigned int)tasks[index].usStackHighWaterMark);
    }
    printf("+------------------+------------+----------+---------+------------------+\n");
    printf("CPU %% is the runtime-statistics share since boot; totals may exceed 100%% on two cores.\n");
    if (count == SYSTEM_CONSOLE_MAX_TASKS) {
        printf("warning=task snapshot reached limit %u\n", SYSTEM_CONSOLE_MAX_TASKS);
    }
}
#endif

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
        print_memory();
        return 0;
    }
    if (strcmp(argv[1], "tasks") == 0) {
#if CONFIG_BLE_GATEWAY_SYS_TASKS_ENABLED
        print_tasks();
#else
        printf("sys tasks is disabled; enable FreeRTOS Trace Facility and BLE Gateway diagnostics in menuconfig\n");
#endif
        return 0;
    }
    printf("usage: sys status | mem | tasks\n");
    return 1;
}

static int system_memory_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    print_memory();
    return 0;
}

void system_console_register(void)
{
    const esp_console_cmd_t system_command_definition = {
        .command = "sys",
        .help = "read-only gateway diagnostics: status, mem, tasks",
        .func = &system_command,
    };
    const esp_console_cmd_t memory_command_definition = {
        .command = "sysmem",
        .help = "alias for: sys mem",
        .func = &system_memory_command,
    };

    (void)esp_console_cmd_register(&system_command_definition);
    (void)esp_console_cmd_register(&memory_command_definition);
}
