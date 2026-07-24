#include "device_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "device_manager";
static QueueHandle_t manager_event_queue;
static device_registry_t registry;

static uint32_t manager_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
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
    size_t index;
    device_registry_result_t result = device_registry_process_report(&registry, report, manager_now_ms(), &index);

    switch (result) {
    case DEVICE_REGISTRY_ADDED:
        ESP_LOGI(TAG, "device added: %s", report->name);
        manager_publish(DEVICE_MANAGER_EVENT_DEVICE_ADDED, &registry.devices[index]);
        break;
    case DEVICE_REGISTRY_UPDATED:
        manager_publish(DEVICE_MANAGER_EVENT_DEVICE_UPDATED, &registry.devices[index]);
        break;
    case DEVICE_REGISTRY_ONLINE:
        manager_publish(DEVICE_MANAGER_EVENT_DEVICE_ONLINE, &registry.devices[index]);
        break;
    case DEVICE_REGISTRY_FULL:
        ESP_LOGW(TAG, "device table full; report dropped");
        break;
    default:
        break;
    }
}

static void manager_mark_offline_devices(void)
{
    const uint32_t now_ms = manager_now_ms();

    size_t index;

    while (device_registry_mark_next_offline(&registry, now_ms, &index) == DEVICE_REGISTRY_OFFLINE) {
        ESP_LOGI(TAG, "device offline: %s", registry.devices[index].report.name);
        manager_publish(DEVICE_MANAGER_EVENT_DEVICE_OFFLINE, &registry.devices[index]);
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
