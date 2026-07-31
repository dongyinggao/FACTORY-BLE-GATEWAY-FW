#include "gateway_publisher.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "csv_logger.h"
#include "ble_scanner.h"
#include "device_manager.h"
#include "event_json.h"
#include "gateway_config.h"
#include "mqtt_service.h"
#include "network_manager.h"
#include "outbox.h"
#include "publisher_ack.h"
#include "storage_manager.h"
#include "time_service.h"

static const char *TAG="publisher";
static uint32_t boot_id;
static uint32_t sequence;
static gateway_publisher_ack_t publish_ack;
static uint32_t previous_discovery_report_count;
static uint32_t previous_filter_match_count;

static void report_memory_health(void)
{
    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    if (dma_free < 24576U || dma_largest < 16384U) {
        ESP_LOGW(TAG, "memory headroom low: dma_free=%u dma_largest=%u psram_free=%u",
                 (unsigned int)dma_free, (unsigned int)dma_largest, (unsigned int)psram_free);
    }
}

static void make_broadcast(const device_lifecycle_event_t *event, char json[GATEWAY_JSON_MAX_LEN])
{
    gateway_broadcast_message_t message={0};
    bool synced;
    message.type=event->type==DEVICE_LIFECYCLE_BROADCAST_STARTED ? GATEWAY_BROADCAST_STARTED : GATEWAY_BROADCAST_ENDED;
    message.device=*event;
    message.event_uptime_s=(message.type==GATEWAY_BROADCAST_STARTED ? event->broadcast_started_ms : event->end_detected_ms)/1000U;
    gateway_event_id_make(message.event_id,sizeof(message.event_id),boot_id,++sequence);
    synced=time_service_format_wall_ms(message.type==GATEWAY_BROADCAST_STARTED ? event->broadcast_started_wall_ms : event->end_detected_wall_ms,message.timestamp,sizeof(message.timestamp));
    message.time_synced=synced;
    if (synced) {
        time_service_format_wall_ms(event->broadcast_started_wall_ms,message.broadcast_started_at,sizeof(message.broadcast_started_at));
        time_service_format_wall_ms(event->last_seen_wall_ms,message.last_seen_at,sizeof(message.last_seen_at));
        if (message.type==GATEWAY_BROADCAST_ENDED) time_service_format_wall_ms(event->end_detected_wall_ms,message.end_detected_at,sizeof(message.end_detected_at));
    }
    gateway_json_encode_broadcast(json,GATEWAY_JSON_MAX_LEN,&message,gateway_config_get());
}
static void enqueue_health(void)
{
    char json[GATEWAY_JSON_MAX_LEN], id[GATEWAY_EVENT_ID_MAX_LEN];
    uint32_t discovery_report_count = ble_scanner_discovery_report_count();
    uint32_t filter_match_count = ble_scanner_filter_match_count();
    gateway_health_message_t message = {
        .event_id = id,
        .config = gateway_config_get(),
        .uptime_s = (uint32_t)(esp_timer_get_time() / 1000000LL),
        .wifi = network_manager_status_text(),
        .mqtt = mqtt_service_status_text(),
        .sntp = time_service_status_text(),
        .sd_ready = csv_logger_is_ready(),
        .sd_status = storage_manager_status_text(),
        .sd_error = storage_manager_last_error(),
        .outbox_messages = gateway_outbox_pending_count(),
        .outbox_bytes = gateway_outbox_pending_bytes(),
        .outbox_failures = gateway_outbox_failure_count(),
        .registered_devices = device_manager_registered_count(),
        .broadcasting_devices = device_manager_broadcasting_count(),
        .scan_reports_30s = discovery_report_count - previous_discovery_report_count,
        .filter_matched_30s = filter_match_count - previous_filter_match_count,
        .scan_queue_high_water = ble_scanner_event_queue_high_watermark(),
        .ui_queue_high_water = device_manager_ui_queue_high_watermark(),
        .capture_queue_high_water = device_manager_capture_queue_high_watermark(),
        .upload_queue_high_water = device_manager_upload_queue_high_watermark(),
        .scan_dropped = ble_scanner_report_drop_count(),
        .ui_dropped = device_manager_ui_drop_count(),
        .capture_dropped = device_manager_capture_drop_count(),
        .upload_dropped = device_manager_upload_drop_count(),
    };

    previous_discovery_report_count = discovery_report_count;
    previous_filter_match_count = filter_match_count;
    gateway_event_id_make(id,sizeof(id),boot_id,++sequence);
    if (gateway_json_encode_health(json, sizeof(json), &message) >= 0) {
        ESP_LOGI(TAG, "health: scan_30s=%lu matched_30s=%lu devices=%u/%u drops=%lu/%lu/%lu/%lu",
                 (unsigned long)message.scan_reports_30s,
                 (unsigned long)message.filter_matched_30s,
                 (unsigned int)message.registered_devices,
                 (unsigned int)message.broadcasting_devices,
                 (unsigned long)message.scan_dropped,
                 (unsigned long)message.ui_dropped,
                 (unsigned long)message.capture_dropped,
                 (unsigned long)message.upload_dropped);
        if (!gateway_outbox_store_health(json) && mqtt_service_is_connected()) {
            ESP_LOGW(TAG, "SD unavailable; sending health without persistent outbox");
            (void)mqtt_service_publish(json);
        }
    }
}
static void publish_next(void)
{
    char json[OUTBOX_MESSAGE_MAX_LEN];
    bool health;
    int message_id;

    if (publish_ack.awaiting || !mqtt_service_is_connected() || !gateway_outbox_next(json, &health)) {
        return;
    }
    message_id = mqtt_service_publish(json);
    if (!gateway_publisher_ack_begin(&publish_ack, message_id)) {
        gateway_outbox_release_current();
        return;
    }
    ESP_LOGD(TAG, "published %s message_id=%d", health ? "health" : "broadcast", message_id);
}
static void publisher_task(void *arg)
{
    QueueHandle_t queue=device_manager_get_upload_queue(); device_lifecycle_event_t event; int message_id; uint32_t last_health=0;
    (void)arg;
    while (true) {
        gateway_outbox_sync_storage();
        if (xQueueReceive(queue,&event,pdMS_TO_TICKS(200))==pdTRUE) {
            char json[GATEWAY_JSON_MAX_LEN]; make_broadcast(&event,json);
            if (gateway_outbox_is_ready()) {
                if (!gateway_outbox_enqueue_broadcast(json)) {
                    ESP_LOGE(TAG,"broadcast persistence failed");
                    if (mqtt_service_is_connected()) {
                        ESP_LOGW(TAG, "sending broadcast without persistent outbox");
                        (void)mqtt_service_publish(json);
                    }
                }
            } else if (mqtt_service_is_connected()) {
                (void)mqtt_service_publish(json);
            }
            else ESP_LOGW(TAG,"SD and MQTT unavailable; broadcast upload dropped");
        }
        while (mqtt_service_take_puback(&message_id)) {
            if (gateway_publisher_ack_accept(&publish_ack, message_id)) {
                gateway_outbox_ack_current();
                ESP_LOGD(TAG, "PUBACK received: message_id=%d", message_id);
            } else {
                ESP_LOGW(TAG, "ignoring unmatched PUBACK: message_id=%d", message_id);
            }
        }
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000000LL);
        if ((uint32_t)(now-last_health)>=30U) {
            enqueue_health();
            report_memory_health();
            last_health=now;
        }
        if (!mqtt_service_is_connected() && publish_ack.awaiting) {
            gateway_outbox_release_current();
            gateway_publisher_ack_reset(&publish_ack);
            ESP_LOGW(TAG, "MQTT disconnected; current outbox record retained for retry");
        }
        publish_next();
    }
}
void gateway_publisher_start(void)
{
    boot_id=esp_random();
    gateway_publisher_ack_reset(&publish_ack);
    gateway_outbox_init();
    xTaskCreate(publisher_task,"publisher",6144,NULL,4,NULL);
    ESP_LOGI(TAG,"publisher boot_id=%08lX",(unsigned long)boot_id);
}
