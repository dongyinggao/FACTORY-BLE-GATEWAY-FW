#include "network_manager.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "gateway_config.h"
#include "time_service.h"

static const char *TAG = "wifi_manager";
static bool connected;
static bool initialized;
static bool task_started;
static uint32_t applied_revision = UINT32_MAX;
static uint32_t attempted_revision = UINT32_MAX;
static uint32_t reconnect_delay_ms = 1000;

static void wifi_apply(void)
{
    const gateway_config_t *config = gateway_config_get();
    wifi_config_t wifi = {0};
    esp_err_t result;

    if (!gateway_config_wifi_is_valid(config)) {
        return;
    }
    memcpy(wifi.sta.ssid, config->wifi_ssid, sizeof(wifi.sta.ssid) - 1U);
    memcpy(wifi.sta.password, config->wifi_password, sizeof(wifi.sta.password) - 1U);
    wifi.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    result = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "unable to apply Wi-Fi configuration: %s", esp_err_to_name(result));
        return;
    }
    result = esp_wifi_connect();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connect request failed: %s", esp_err_to_name(result));
        return;
    }
    ESP_LOGI(TAG, "connecting to configured Wi-Fi SSID");
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_START) {
        wifi_apply();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    connected = true;
    reconnect_delay_ms = 1000;
    ESP_LOGI(TAG, "Wi-Fi connected");
    time_service_start_sync();
}

static bool wifi_initialize(void)
{
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t result;

    result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        goto failed;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        goto failed;
    }
    esp_netif_create_default_wifi_sta();
    result = esp_wifi_init(&init);
    if (result != ESP_OK) {
        goto failed;
    }
    result = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (result != ESP_OK) {
        goto failed;
    }
    result = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL);
    if (result != ESP_OK) {
        goto failed;
    }
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) {
        goto failed;
    }
    result = esp_wifi_start();
    if (result != ESP_OK) {
        goto failed;
    }
    initialized = true;
    return true;

failed:
    ESP_LOGE(TAG, "Wi-Fi unavailable (%s); BLE and CSV continue", esp_err_to_name(result));
    return false;
}

static void network_task(void *arg)
{
    (void)arg;
    while (true) {
        const gateway_config_t *config = gateway_config_get();
        uint32_t revision = gateway_config_get_revision();

        if (!gateway_config_wifi_is_valid(config)) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!initialized) {
            if (attempted_revision != revision) {
                attempted_revision = revision;
                wifi_initialize();
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (revision != applied_revision) {
            applied_revision = revision;
            esp_wifi_disconnect();
            wifi_apply();
        }
        if (!connected) {
            vTaskDelay(pdMS_TO_TICKS(reconnect_delay_ms));
            esp_wifi_connect();
            if (reconnect_delay_ms < 30000) {
                reconnect_delay_ms *= 2;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void network_manager_start(void)
{
    if (task_started) {
        return;
    }
    task_started = true;
    xTaskCreate(network_task, "wifi_manager", 4096, NULL, 4, NULL);
}

bool network_manager_is_connected(void)
{
    return connected;
}

const char *network_manager_status_text(void)
{
    if (!gateway_config_wifi_is_valid(gateway_config_get())) {
        return "Disabled";
    }
    if (!initialized) {
        return attempted_revision == gateway_config_get_revision() ? "Error" : "Starting";
    }
    return connected ? "Connected" : "Connecting";
}
