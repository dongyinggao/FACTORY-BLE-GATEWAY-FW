#include "device_manager.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "device_manager";
static QueueHandle_t manager_event_queue;
static managed_device_t devices[DEVICE_MANAGER_MAX_DEVICES];

static uint32_t manager_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static bool manager_same_address(const managed_device_t *device, const ble_scan_report_t *report)
{
    return device->report.address_type == report->address_type &&
           memcmp(device->report.address, report->address, sizeof(report->address)) == 0;
}

static int manager_find_device(const ble_scan_report_t *report)
{
    for (int index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        if (devices[index].report.name[0] != '\0' && manager_same_address(&devices[index], report)) {
            return index;
        }
    }
    return -1;
}

static int manager_find_empty_slot(void)
{
    for (int index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        if (devices[index].report.name[0] == '\0') {
            return index;
        }
    }
    return -1;
}

static void manager_publish(device_manager_event_type_t type, const managed_device_t *device)
{
    device_manager_event_t event = {
        .type = type,
    };

    if (device != NULL) {
        event.device = *device;
    }

    if (xQueueSend(manager_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "manager event queue full; event dropped");
    }
}

static void manager_publish_scanner_state(const ble_scanner_event_t *scanner_event)
{
    device_manager_event_t event = {
        .type = DEVICE_MANAGER_EVENT_SCANNER_STATE,
        .scanner_state = scanner_event->state,
        .error_code = scanner_event->error_code,
    };

    if (xQueueSend(manager_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "manager event queue full; scanner state dropped");
    }
}

static void manager_process_report(const ble_scan_report_t *report)
{
    int index = manager_find_device(report);
    bool was_online = false;

    if (index < 0) {
        index = manager_find_empty_slot();
        if (index < 0) {
            ESP_LOGW(TAG, "device table full; report dropped");
            return;
        }
        devices[index].report = *report;
        devices[index].online = true;
        devices[index].last_seen_ms = manager_now_ms();
        ESP_LOGI(TAG, "device added: %s", report->name);
        manager_publish(DEVICE_MANAGER_EVENT_DEVICE_ADDED, &devices[index]);
        return;
    }

    was_online = devices[index].online;
    devices[index].report = *report;
    devices[index].online = true;
    devices[index].last_seen_ms = manager_now_ms();
    manager_publish(was_online ? DEVICE_MANAGER_EVENT_DEVICE_UPDATED : DEVICE_MANAGER_EVENT_DEVICE_ONLINE,
                    &devices[index]);
}

static void manager_mark_offline_devices(void)
{
    const uint32_t now_ms = manager_now_ms();

    for (int index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        managed_device_t *device = &devices[index];
        if (device->report.name[0] == '\0' || !device->online) {
            continue;
        }
        if ((uint32_t)(now_ms - device->last_seen_ms) >= DEVICE_MANAGER_OFFLINE_TIMEOUT_MS) {
            device->online = false;
            ESP_LOGI(TAG, "device offline: %s", device->report.name);
            manager_publish(DEVICE_MANAGER_EVENT_DEVICE_OFFLINE, device);
        }
    }
}

static void device_manager_task(void *parameter)
{
    QueueHandle_t scanner_queue = ble_scanner_get_event_queue();
    ble_scanner_event_t scanner_event;

    (void)parameter;
    while (true) {
        if (xQueueReceive(scanner_queue, &scanner_event, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (scanner_event.type == BLE_SCANNER_EVENT_STATE) {
                manager_publish_scanner_state(&scanner_event);
            } else if (scanner_event.type == BLE_SCANNER_EVENT_REPORT) {
                manager_process_report(&scanner_event.report);
            }
        }
        manager_mark_offline_devices();
    }
}

void device_manager_init(void)
{
    manager_event_queue = xQueueCreate(BLE_EVENT_QUEUE_MAX_LEN, sizeof(device_manager_event_t));
    configASSERT(manager_event_queue != NULL);
    xTaskCreate(device_manager_task, "device_manager", 4096, NULL, 5, NULL);
}

QueueHandle_t device_manager_get_event_queue(void)
{
    return manager_event_queue;
}
