#include "mqtt_service.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "gateway_config.h"
#include "network_manager.h"

static const char *TAG="mqtt_service";
static esp_mqtt_client_handle_t client;
static QueueHandle_t puback_queue;
static bool connected;
static uint32_t applied_revision=UINT32_MAX;
static char mqtt_client_id[48];
static void mqtt_event(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event=event_data; (void)args;(void)base;
    if (event_id==MQTT_EVENT_CONNECTED) { connected=true; ESP_LOGI(TAG,"MQTT connected"); }
    else if (event_id==MQTT_EVENT_DISCONNECTED) { connected=false; ESP_LOGW(TAG,"MQTT disconnected"); }
    else if (event_id==MQTT_EVENT_PUBLISHED && puback_queue) xQueueSend(puback_queue,&event->msg_id,0);
}
static void mqtt_reload(void)
{
    const gateway_config_t *c=gateway_config_get(); esp_mqtt_client_config_t cfg={0};
    if (client) { esp_mqtt_client_stop(client); esp_mqtt_client_destroy(client); client=NULL; connected=false; }
    if (!gateway_config_mqtt_is_valid(c)) { ESP_LOGW(TAG,"MQTT not configured"); return; }
    snprintf(mqtt_client_id,sizeof(mqtt_client_id),"gateway-%s",c->gateway_id[0]?c->gateway_id:"unassigned");
    cfg.broker.address.uri=c->mqtt_uri; cfg.credentials.username=c->mqtt_username[0]?c->mqtt_username:NULL; cfg.credentials.authentication.password=c->mqtt_password[0]?c->mqtt_password:NULL; cfg.credentials.client_id=mqtt_client_id;
    client=esp_mqtt_client_init(&cfg); if (!client) return;
    esp_mqtt_client_register_event(client,ESP_EVENT_ANY_ID,mqtt_event,NULL); esp_mqtt_client_start(client);
}
static void mqtt_task(void *arg)
{
    (void)arg;
    while (true) {
        if (gateway_config_get_revision() != applied_revision) {
            applied_revision = gateway_config_get_revision();
            mqtt_reload();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void mqtt_service_start(void)
{
    puback_queue=xQueueCreate(8,sizeof(int)); applied_revision=gateway_config_get_revision(); mqtt_reload();
    xTaskCreate(mqtt_task,"mqtt_service",4096,NULL,4,NULL);
}
bool mqtt_service_is_connected(void) { return connected && network_manager_is_connected(); }
int mqtt_service_publish(const char *payload)
{
    char topic[128]; const gateway_config_t *c=gateway_config_get();
    if (!mqtt_service_is_connected() || !payload) return -1;
    snprintf(topic,sizeof(topic),"factory/product-status/gateway/%s/events",c->gateway_id[0]?c->gateway_id:"unassigned");
    return esp_mqtt_client_publish(client,topic,payload,0,1,0);
}
bool mqtt_service_take_puback(int *message_id) { return puback_queue && xQueueReceive(puback_queue,message_id,0)==pdTRUE; }
const char *mqtt_service_status_text(void) { return !gateway_config_mqtt_is_valid(gateway_config_get()) ? "Disabled" : (connected ? "Connected" : "Connecting"); }
