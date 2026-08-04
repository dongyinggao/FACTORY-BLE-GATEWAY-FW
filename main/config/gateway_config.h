#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GATEWAY_ID_MAX_LEN 32
#define GATEWAY_LOCATION_MAX_LEN 48
#define GATEWAY_WIFI_SSID_MAX_LEN 33
#define GATEWAY_WIFI_PASSWORD_MAX_LEN 65
#define GATEWAY_MQTT_URI_MAX_LEN 128
#define GATEWAY_MQTT_USERNAME_MAX_LEN 64
#define GATEWAY_MQTT_PASSWORD_MAX_LEN 64
#define GATEWAY_NTP_SERVER_MAX_LEN 64
#define GATEWAY_TIMEZONE_MAX_LEN 40
#define GATEWAY_NAME_RULES_MAX_LEN 128
#define GATEWAY_OTA_MANIFEST_URI_MAX_LEN 192

typedef struct {
    char gateway_id[GATEWAY_ID_MAX_LEN];
    char gateway_location[GATEWAY_LOCATION_MAX_LEN];
    uint32_t broadcast_end_ms;
    char wifi_ssid[GATEWAY_WIFI_SSID_MAX_LEN];
    char wifi_password[GATEWAY_WIFI_PASSWORD_MAX_LEN];
    char mqtt_uri[GATEWAY_MQTT_URI_MAX_LEN];
    char mqtt_username[GATEWAY_MQTT_USERNAME_MAX_LEN];
    char mqtt_password[GATEWAY_MQTT_PASSWORD_MAX_LEN];
    uint8_t mqtt_qos;
    char ntp_server[GATEWAY_NTP_SERVER_MAX_LEN];
    char timezone[GATEWAY_TIMEZONE_MAX_LEN];
    char name_rules[GATEWAY_NAME_RULES_MAX_LEN];
    char ota_manifest_uri[GATEWAY_OTA_MANIFEST_URI_MAX_LEN];
    uint32_t config_ver;
} gateway_config_t;

void gateway_config_init(void);
const gateway_config_t *gateway_config_get(void);
bool gateway_config_is_complete(void);
bool gateway_config_wifi_is_valid(const gateway_config_t *config);
bool gateway_config_mqtt_is_valid(const gateway_config_t *config);
bool gateway_config_ota_is_valid(const gateway_config_t *config);
uint32_t gateway_config_get_revision(void);

/* Starts the USB Serial/JTAG `cfg` console without exposing generic NVS access. */
void gateway_config_console_start(void);
