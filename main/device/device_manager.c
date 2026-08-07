#include "device_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

#include "gateway_config.h"

static const char *TAG = "device_manager";
static QueueHandle_t ui_event_queue;
static QueueHandle_t capture_event_queue;
static QueueHandle_t upload_event_queue;
static QueueHandle_t activity_event_queue;
static device_registry_t registry;
static device_observation_clock_t observation_clock;
static uint32_t capture_drop_count;
static uint32_t upload_drop_count;
static uint32_t activity_drop_count;
static uint32_t ui_drop_count;
static uint32_t table_reject_count;
static uint32_t ui_queue_high_watermark;
static uint32_t capture_queue_high_watermark;
static uint32_t upload_queue_high_watermark;
static uint32_t broadcast_boot_id;
static uint32_t broadcast_sequence;
static volatile uint16_t registered_count;
static volatile uint16_t broadcasting_count;

static uint32_t manager_now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* Absolute-time formatting uses esp_timer in time_service. Keep lifecycle
 * timestamps on that same clock; application uptime remains tick-based. */
static uint64_t manager_wall_timebase_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static uint16_t manager_device_count(bool broadcasting_only)
{
    uint16_t count = 0;
    for (size_t index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        if (registry.devices[index].report.name[0] != '\0' &&
            (!broadcasting_only || registry.devices[index].broadcasting)) {
            ++count;
        }
    }
    return count;
}

static void manager_track_queue_high_watermark(QueueHandle_t queue, uint32_t *high_watermark)
{
    uint32_t depth = uxQueueMessagesWaiting(queue);
    if (depth > *high_watermark) {
        *high_watermark = depth;
    }
}

static void manager_enqueue_ui(const device_manager_ui_event_t *event)
{
    if (xQueueSend(ui_event_queue, event, 0) == pdTRUE) {
        manager_track_queue_high_watermark(ui_event_queue, &ui_queue_high_watermark);
        return;
    }
    ++ui_drop_count;
    if (ui_drop_count == 1U || (ui_drop_count % 10U) == 0U) {
        ESP_LOGW(TAG, "UI event queue full; events dropped=%lu", (unsigned long)ui_drop_count);
    }
}

static void manager_publish_ui(const managed_device_t *device)
{
    registered_count = manager_device_count(false);
    broadcasting_count = manager_device_count(true);
    device_manager_ui_event_t event = {
        .type = DEVICE_MANAGER_EVENT_DEVICE_CHANGED,
        .device_count = registered_count,
        .broadcasting_count = broadcasting_count,
    };

    if (device != NULL) {
        memcpy(event.name, device->report.name, sizeof(event.name));
        memcpy(event.address, device->report.address, sizeof(event.address));
        event.rssi = device->report.rssi;
        event.broadcasting = device->broadcasting;
    }
    manager_enqueue_ui(&event);
}

static void manager_publish_scanner_state(const ble_scanner_event_t *scanner_event)
{
    registered_count = manager_device_count(false);
    broadcasting_count = manager_device_count(true);
    device_manager_ui_event_t event = {
        .type = DEVICE_MANAGER_EVENT_SCANNER_STATE,
        .scanner_state = scanner_event->state,
        .error_code = scanner_event->error_code,
        .device_count = registered_count,
        .broadcasting_count = broadcasting_count,
    };

    manager_enqueue_ui(&event);
}

static void manager_publish_lifecycle(device_lifecycle_event_type_t type,
                                      const managed_device_t *device)
{
    device_lifecycle_event_t event = {
        .type = type,
        .rssi = device->report.rssi,
        .broadcast_started_ms = device->broadcast_started_ms,
        .last_seen_ms = device->last_seen_ms,
        .end_detected_ms = device->end_detected_ms,
        .broadcast_started_wall_ms = device->broadcast_started_wall_ms,
        .last_seen_wall_ms = device->last_seen_wall_ms,
        .end_detected_wall_ms = device->end_detected_wall_ms,
    };
    memcpy(event.broadcast_id, device->broadcast_id, sizeof(event.broadcast_id));
    memcpy(event.name, device->report.name, sizeof(event.name));
    memcpy(event.address, device->report.address, sizeof(event.address));

    if (xQueueSend(capture_event_queue, &event, 0) != pdTRUE) {
        ++capture_drop_count;
        ESP_LOGE(TAG, "capture queue full; lifecycle event lost");
    } else {
        manager_track_queue_high_watermark(capture_event_queue, &capture_queue_high_watermark);
    }
    if (xQueueSend(upload_event_queue, &event, 0) != pdTRUE) {
        ++upload_drop_count;
        ESP_LOGE(TAG, "upload queue full; lifecycle event lost");
    } else {
        manager_track_queue_high_watermark(upload_event_queue, &upload_queue_high_watermark);
    }
}

/* Activity observations are intentionally low priority: they make a long
 * broadcast visible to the server but must never consume the durable CSV /
 * Outbox path used by start and end records. */
static void manager_publish_activity(const managed_device_t *device)
{
    device_lifecycle_event_t event = {
        .type = DEVICE_LIFECYCLE_BROADCAST_ACTIVE,
        .rssi = device->report.rssi,
        .broadcast_started_ms = device->broadcast_started_ms,
        .last_seen_ms = device->last_seen_ms,
        .broadcast_started_wall_ms = device->broadcast_started_wall_ms,
        .last_seen_wall_ms = device->last_seen_wall_ms,
    };

    memcpy(event.broadcast_id, device->broadcast_id, sizeof(event.broadcast_id));
    memcpy(event.name, device->report.name, sizeof(event.name));
    memcpy(event.address, device->report.address, sizeof(event.address));
    if (xQueueSend(activity_event_queue, &event, 0) == pdTRUE) {
        return;
    }
    ++activity_drop_count;
    if (activity_drop_count == 1U || (activity_drop_count % 10U) == 0U) {
        ESP_LOGW(TAG, "activity queue full; observations dropped=%lu",
                 (unsigned long)activity_drop_count);
    }
}

static void manager_assign_broadcast_id(managed_device_t *device)
{
    ++broadcast_sequence;
    snprintf(device->broadcast_id, sizeof(device->broadcast_id), "%08lX-%lu",
             (unsigned long)broadcast_boot_id, (unsigned long)broadcast_sequence);
}

static void manager_handle_scanner_state(const ble_scanner_event_t *scanner_event, uint32_t wall_ms)
{
    device_observation_clock_set_observing(&observation_clock,
                                            scanner_event->state == BLE_SCANNER_STATE_SCANNING,
                                            wall_ms);
    manager_publish_scanner_state(scanner_event);
}

static void manager_process_report(const ble_scan_report_t *report, uint64_t wall_ms)
{
    size_t index;
    device_registry_result_t result = device_registry_process_report(&registry,
                                                                       report,
                                                                       device_observation_clock_now(&observation_clock),
                                                                       wall_ms,
                                                                       &index);

    switch (result) {
    case DEVICE_REGISTRY_ADDED:
        ESP_LOGI(TAG, "device added: %s", report->name);
        manager_assign_broadcast_id(&registry.devices[index]);
        registry.devices[index].last_active_published_ms = registry.devices[index].broadcast_started_ms;
        manager_publish_ui(&registry.devices[index]);
        manager_publish_lifecycle(DEVICE_LIFECYCLE_BROADCAST_STARTED, &registry.devices[index]);
        break;
    case DEVICE_REGISTRY_BROADCAST_STARTED:
        manager_assign_broadcast_id(&registry.devices[index]);
        registry.devices[index].last_active_published_ms = registry.devices[index].broadcast_started_ms;
        manager_publish_ui(&registry.devices[index]);
        manager_publish_lifecycle(DEVICE_LIFECYCLE_BROADCAST_STARTED, &registry.devices[index]);
        break;
    case DEVICE_REGISTRY_FULL:
        ++table_reject_count;
        if (table_reject_count == 1U || (table_reject_count % 10U) == 0U) {
            ESP_LOGW(TAG, "device table full; valid reports rejected=%lu",
                     (unsigned long)table_reject_count);
        }
        break;
    default:
        break;
    }
}

static void manager_publish_active_broadcasts(uint32_t now_ms)
{
    if (!observation_clock.observing) {
        return;
    }
    for (size_t index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        managed_device_t *device = &registry.devices[index];
        if (!device->broadcasting || device->report.name[0] == '\0' ||
            (uint32_t)(now_ms - device->last_active_published_ms) <
                DEVICE_MANAGER_BROADCAST_ACTIVE_INTERVAL_MS) {
            continue;
        }
        device->last_active_published_ms = now_ms;
        manager_publish_activity(device);
    }
}

static void manager_mark_ended_broadcasts(uint64_t wall_ms)
{
    size_t index;

    while (device_registry_mark_next_broadcast_ended(&registry,
                                                       device_observation_clock_now(&observation_clock),
                                                       wall_ms,
                                                       gateway_config_get()->broadcast_end_ms,
                                                       &index) == DEVICE_REGISTRY_BROADCAST_ENDED) {
        ESP_LOGI(TAG, "broadcast ended: %s", registry.devices[index].report.name);
        manager_publish_ui(&registry.devices[index]);
        manager_publish_lifecycle(DEVICE_LIFECYCLE_BROADCAST_ENDED, &registry.devices[index]);
    }
}

static void device_manager_task(void *parameter)
{
    QueueHandle_t scanner_queue = ble_scanner_get_event_queue();
    ble_scanner_event_t scanner_event;

    (void)parameter;
    while (true) {
        BaseType_t received;
        uint32_t now_ms;
        uint64_t wall_ms;

        received = xQueueReceive(scanner_queue, &scanner_event, pdMS_TO_TICKS(1000));
        now_ms = manager_now_ms();
        wall_ms = manager_wall_timebase_ms();
        device_observation_clock_tick(&observation_clock, now_ms);

        if (received == pdTRUE) {
            if (scanner_event.type == BLE_SCANNER_EVENT_STATE) {
                manager_handle_scanner_state(&scanner_event, now_ms);
            } else if (scanner_event.type == BLE_SCANNER_EVENT_REPORT) {
                int64_t queue_wait_us = esp_timer_get_time() - scanner_event.enqueued_at_us;
                ble_scanner_record_report_queue_wait_us(queue_wait_us > UINT32_MAX ?
                                                          UINT32_MAX : (uint32_t)queue_wait_us);
                manager_process_report(&scanner_event.report, wall_ms);
            }
        }
        manager_mark_ended_broadcasts(wall_ms);
        manager_publish_active_broadcasts(device_observation_clock_now(&observation_clock));
    }
}

void device_manager_init(void)
{
    ui_event_queue = xQueueCreate(DEVICE_MANAGER_UI_QUEUE_LEN, sizeof(device_manager_ui_event_t));
    configASSERT(ui_event_queue != NULL);
    capture_event_queue = xQueueCreateWithCaps(DEVICE_MANAGER_LIFECYCLE_QUEUE_LEN,
                                                sizeof(device_lifecycle_event_t),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    configASSERT(capture_event_queue != NULL);
    upload_event_queue = xQueueCreateWithCaps(DEVICE_MANAGER_LIFECYCLE_QUEUE_LEN,
                                               sizeof(device_lifecycle_event_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    configASSERT(upload_event_queue != NULL);
    activity_event_queue = xQueueCreateWithCaps(DEVICE_MANAGER_ACTIVITY_QUEUE_LEN,
                                                 sizeof(device_lifecycle_event_t),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    configASSERT(activity_event_queue != NULL);
    device_observation_clock_init(&observation_clock, manager_now_ms());
    broadcast_boot_id = esp_random();
    broadcast_sequence = 0;
    xTaskCreate(device_manager_task, "device_manager", 4096, NULL, 5, NULL);
}

QueueHandle_t device_manager_get_ui_event_queue(void)
{
    return ui_event_queue;
}

void device_manager_request_ui_status_refresh(void)
{
    const device_manager_ui_event_t event = {
        .type = DEVICE_MANAGER_EVENT_STATUS_CHANGED,
    };

    if (ui_event_queue != NULL) {
        manager_enqueue_ui(&event);
    }
}

QueueHandle_t device_manager_get_capture_queue(void)
{
    return capture_event_queue;
}

QueueHandle_t device_manager_get_upload_queue(void)
{
    return upload_event_queue;
}

QueueHandle_t device_manager_get_activity_queue(void)
{
    return activity_event_queue;
}

uint32_t device_manager_capture_drop_count(void)
{
    return capture_drop_count;
}

uint32_t device_manager_upload_drop_count(void)
{
    return upload_drop_count;
}

uint32_t device_manager_activity_drop_count(void)
{
    return activity_drop_count;
}

uint32_t device_manager_table_reject_count(void)
{
    return table_reject_count;
}

uint32_t device_manager_ui_drop_count(void)
{
    return ui_drop_count;
}

uint16_t device_manager_registered_count(void)
{
    return registered_count;
}

uint16_t device_manager_broadcasting_count(void)
{
    return broadcasting_count;
}

uint32_t device_manager_ui_queue_depth(void)
{
    return ui_event_queue == NULL ? 0U : uxQueueMessagesWaiting(ui_event_queue);
}

uint32_t device_manager_capture_queue_depth(void)
{
    return capture_event_queue == NULL ? 0U : uxQueueMessagesWaiting(capture_event_queue);
}

uint32_t device_manager_upload_queue_depth(void)
{
    return upload_event_queue == NULL ? 0U : uxQueueMessagesWaiting(upload_event_queue);
}

uint32_t device_manager_ui_queue_high_watermark(void)
{
    return ui_queue_high_watermark;
}

uint32_t device_manager_capture_queue_high_watermark(void)
{
    return capture_queue_high_watermark;
}

uint32_t device_manager_upload_queue_high_watermark(void)
{
    return upload_queue_high_watermark;
}
