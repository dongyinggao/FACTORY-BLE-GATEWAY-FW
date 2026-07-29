#include "gateway_publisher.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "csv_logger.h"
#include "device_manager.h"
#include "event_json.h"
#include "gateway_config.h"
#include "mqtt_service.h"
#include "network_manager.h"
#include "outbox.h"
#include "time_service.h"

static const char *TAG="publisher";
static uint32_t boot_id;
static uint32_t sequence;
static bool awaiting_ack;

static void report_memory_health(void)
{
    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    if (dma_free < 24576U || dma_largest < 16384U) {
        ESP_LOGW(TAG, "memory headroom low: dma_free=%u dma_largest=%u psram_free=%u",
                 (unsigned int)dma_free, (unsigned int)dma_largest, (unsigned int)psram_free);
    } else {
        ESP_LOGI(TAG, "memory: dma_free=%u dma_largest=%u psram_free=%u",
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
    gateway_event_id_make(id,sizeof(id),boot_id,++sequence);
    if (gateway_json_encode_health(json,sizeof(json),id,gateway_config_get(),(uint32_t)(esp_timer_get_time()/1000000LL),network_manager_status_text(),mqtt_service_status_text(),time_service_status_text(),csv_logger_is_ready(),gateway_outbox_pending_count(),device_manager_capture_drop_count(),device_manager_upload_drop_count()) >= 0)
        gateway_outbox_store_health(json);
}
static void publish_next(void)
{
    char json[OUTBOX_MESSAGE_MAX_LEN]; bool health;
    if (awaiting_ack || !mqtt_service_is_connected() || !gateway_outbox_next(json,&health)) return;
    if (mqtt_service_publish(json) < 0) { gateway_outbox_release_current(); return; }
    awaiting_ack=true;
}
static void publisher_task(void *arg)
{
    QueueHandle_t queue=device_manager_get_upload_queue(); device_lifecycle_event_t event; int message_id; uint32_t last_health=0;
    (void)arg;
    while (true) {
        if (xQueueReceive(queue,&event,pdMS_TO_TICKS(200))==pdTRUE) {
            char json[GATEWAY_JSON_MAX_LEN]; make_broadcast(&event,json);
            if (gateway_outbox_is_ready()) { if (!gateway_outbox_enqueue_broadcast(json)) ESP_LOGE(TAG,"broadcast persistence failed"); }
            else if (mqtt_service_is_connected()) mqtt_service_publish(json);
            else ESP_LOGW(TAG,"SD and MQTT unavailable; broadcast upload dropped");
        }
        while (mqtt_service_take_puback(&message_id)) { (void)message_id; if (awaiting_ack) { gateway_outbox_ack_current(); awaiting_ack=false; ESP_LOGD(TAG,"PUBACK received"); } }
        uint32_t now=(uint32_t)(esp_timer_get_time()/1000000LL);
        if ((uint32_t)(now-last_health)>=30U) {
            enqueue_health();
            report_memory_health();
            last_health=now;
        }
        if (!mqtt_service_is_connected() && awaiting_ack) { gateway_outbox_release_current(); awaiting_ack=false; }
        publish_next();
    }
}
void gateway_publisher_start(void)
{
    boot_id=esp_random(); gateway_outbox_init(); xTaskCreate(publisher_task,"publisher",6144,NULL,4,NULL);
    ESP_LOGI(TAG,"publisher boot_id=%08lX",(unsigned long)boot_id);
}
