#include "mqtt_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "gateway_config.h"
#include "network_manager.h"
#include "device_manager.h"

static const char *TAG="mqtt_service";
static esp_mqtt_client_handle_t client;
static QueueHandle_t puback_queue;
static QueueHandle_t ota_command_queue;
static bool connected;
static uint32_t applied_revision=UINT32_MAX;
static uint32_t ota_command_dropped;
static char mqtt_client_id[48];

#define MQTT_OTA_COMMAND_MAX_LEN 512U
#define MQTT_OTA_COMMAND_QUEUE_LEN 4U

typedef struct {
    char payload[MQTT_OTA_COMMAND_MAX_LEN];
} mqtt_ota_command_message_t;

static mqtt_service_ota_command_handler_t ota_command_handler;

static void mqtt_ota_command_topic(char *topic, size_t topic_size)
{
    const gateway_config_t *config = gateway_config_get();

    snprintf(topic, topic_size, "factory/product-status/gateway/%s/commands/ota",
             config->gateway_id[0] ? config->gateway_id : "unassigned");
}

static void mqtt_event(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event=event_data; (void)args;(void)base;
    if (event_id==MQTT_EVENT_CONNECTED) {
        char topic[160];

        connected=true;
        mqtt_ota_command_topic(topic, sizeof(topic));
        if (esp_mqtt_client_subscribe(client, topic, 1) < 0) {
            ESP_LOGW(TAG, "cannot subscribe OTA command topic");
        } else {
            ESP_LOGI(TAG, "MQTT connected; subscribed OTA command topic");
        }
        device_manager_request_ui_status_refresh();
    }
    else if (event_id==MQTT_EVENT_DISCONNECTED) { connected=false; ESP_LOGW(TAG,"MQTT disconnected"); device_manager_request_ui_status_refresh(); }
    else if (event_id==MQTT_EVENT_PUBLISHED && puback_queue) xQueueSend(puback_queue,&event->msg_id,0);
    else if (event_id == MQTT_EVENT_DATA && ota_command_queue != NULL) {
        char topic[160];
        mqtt_ota_command_message_t message = {0};

        mqtt_ota_command_topic(topic, sizeof(topic));
        if (event->topic_len != (int)strlen(topic) ||
            memcmp(event->topic, topic, (size_t)event->topic_len) != 0) {
            return;
        }
        if (event->current_data_offset != 0 || event->data_len != event->total_data_len ||
            event->data_len <= 0 || event->data_len >= (int)sizeof(message.payload)) {
            ++ota_command_dropped;
            ESP_LOGW(TAG, "dropped fragmented or oversized OTA command");
            return;
        }
        memcpy(message.payload, event->data, (size_t)event->data_len);
        if (xQueueSend(ota_command_queue, &message, 0) != pdTRUE) {
            ++ota_command_dropped;
            ESP_LOGW(TAG, "OTA command queue full");
        }
    }
}
static void mqtt_reload(void)
{
    const gateway_config_t *c=gateway_config_get(); esp_mqtt_client_config_t cfg={0};
    if (client) { esp_mqtt_client_stop(client); esp_mqtt_client_destroy(client); client=NULL; connected=false; }
    if (!gateway_config_mqtt_is_valid(c)) { ESP_LOGW(TAG,"MQTT not configured"); device_manager_request_ui_status_refresh(); return; }
    snprintf(mqtt_client_id,sizeof(mqtt_client_id),"gateway-%s",c->gateway_id[0]?c->gateway_id:"unassigned");
    cfg.broker.address.uri=c->mqtt_uri;
    cfg.broker.verification.crt_bundle_attach=esp_crt_bundle_attach;
    cfg.credentials.username=c->mqtt_username[0]?c->mqtt_username:NULL;
    cfg.credentials.authentication.password=c->mqtt_password[0]?c->mqtt_password:NULL;
    cfg.credentials.client_id=mqtt_client_id;
    client=esp_mqtt_client_init(&cfg); if (!client) return;
    esp_mqtt_client_register_event(client,ESP_EVENT_ANY_ID,mqtt_event,NULL); esp_mqtt_client_start(client);
}
static void mqtt_task(void *arg)
{
    (void)arg;
    while (true) {
        mqtt_ota_command_message_t command;

        if (gateway_config_get_revision() != applied_revision) {
            applied_revision = gateway_config_get_revision();
            mqtt_reload();
        }
        while (ota_command_handler != NULL && ota_command_queue != NULL &&
               xQueueReceive(ota_command_queue, &command, 0) == pdTRUE) {
            ota_command_handler(command.payload);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void mqtt_service_start(void)
{
    puback_queue=xQueueCreate(8,sizeof(int));
    ota_command_queue = xQueueCreate(MQTT_OTA_COMMAND_QUEUE_LEN, sizeof(mqtt_ota_command_message_t));
    configASSERT(puback_queue != NULL && ota_command_queue != NULL);
    applied_revision=gateway_config_get_revision(); mqtt_reload();
    xTaskCreate(mqtt_task,"mqtt_service",4096,NULL,4,NULL);
}
bool mqtt_service_is_connected(void) { return connected && network_manager_is_connected(); }
int mqtt_service_publish(const char *payload)
{
    char topic[128]; const gateway_config_t *c=gateway_config_get();
    snprintf(topic,sizeof(topic),"factory/product-status/gateway/%s/events",c->gateway_id[0]?c->gateway_id:"unassigned");
    return mqtt_service_publish_to_topic(topic, payload);
}
int mqtt_service_publish_to_topic(const char *topic, const char *payload)
{
    if (!mqtt_service_is_connected() || !topic || topic[0] == '\0' || !payload) return -1;
    return esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
}
bool mqtt_service_take_puback(int *message_id) { return puback_queue && xQueueReceive(puback_queue,message_id,0)==pdTRUE; }
const char *mqtt_service_status_text(void) { return !gateway_config_mqtt_is_valid(gateway_config_get()) ? "Disabled" : (connected ? "Connected" : "Connecting"); }
bool mqtt_service_is_secure_transport(void)
{
    return strncmp(gateway_config_get()->mqtt_uri, "mqtts://", 8U) == 0;
}
void mqtt_service_set_ota_command_handler(mqtt_service_ota_command_handler_t handler)
{
    ota_command_handler = handler;
}
uint32_t mqtt_service_ota_command_dropped(void) { return ota_command_dropped; }
