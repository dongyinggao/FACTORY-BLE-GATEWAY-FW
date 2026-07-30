#include "gateway_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

#include "device_filter.h"
#include "device_manager_core.h"

static const char *TAG = "gateway_config";
static const char *NVS_NAMESPACE = "gateway_cfg";
static gateway_config_t active_config;
static gateway_config_t pending_config;
static uint32_t config_revision;

static void load_string(nvs_handle_t handle, const char *key, char *value, size_t value_size)
{
    size_t length = value_size;
    if (nvs_get_str(handle, key, value, &length) != ESP_OK) value[0] = '\0';
}

static void set_defaults(gateway_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->broadcast_end_ms = DEVICE_MANAGER_DEFAULT_BCAST_END_MS;
    config->mqtt_qos = 1;
    snprintf(config->ntp_server, sizeof(config->ntp_server), "pool.ntp.org");
    snprintf(config->timezone, sizeof(config->timezone), "CST-8");
}

void gateway_config_init(void)
{
    nvs_handle_t handle = 0;
    uint32_t seconds = DEVICE_MANAGER_DEFAULT_BCAST_END_MS / 1000U;

    set_defaults(&active_config);
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        load_string(handle, "gateway_id", active_config.gateway_id, sizeof(active_config.gateway_id));
        load_string(handle, "gateway_loc", active_config.gateway_location, sizeof(active_config.gateway_location));
        load_string(handle, "wifi_ssid", active_config.wifi_ssid, sizeof(active_config.wifi_ssid));
        load_string(handle, "wifi_pwd", active_config.wifi_password, sizeof(active_config.wifi_password));
        load_string(handle, "mqtt_uri", active_config.mqtt_uri, sizeof(active_config.mqtt_uri));
        load_string(handle, "mqtt_user", active_config.mqtt_username, sizeof(active_config.mqtt_username));
        load_string(handle, "mqtt_pwd", active_config.mqtt_password, sizeof(active_config.mqtt_password));
        load_string(handle, "ntp_srv", active_config.ntp_server, sizeof(active_config.ntp_server));
        load_string(handle, "timezone", active_config.timezone, sizeof(active_config.timezone));
        load_string(handle, "name_rules", active_config.name_rules, sizeof(active_config.name_rules));
        if (nvs_get_u32(handle, "bcast_end_s", &seconds) == ESP_OK && seconds >= 5 && seconds <= 300)
            active_config.broadcast_end_ms = seconds * 1000U;
        nvs_get_u8(handle, "mqtt_qos", &active_config.mqtt_qos);
        nvs_get_u32(handle, "config_ver", &active_config.config_ver);
        nvs_close(handle);
    }
    if (active_config.mqtt_qos != 1) active_config.mqtt_qos = 1;
    pending_config = active_config;
    if (active_config.name_rules[0] != '\0' && !device_filter_set_rules(active_config.name_rules))
        ESP_LOGW(TAG, "invalid name_rules in NVS; default rules enabled");
}

const gateway_config_t *gateway_config_get(void) { return &active_config; }
bool gateway_config_is_complete(void) { return active_config.gateway_id[0] != '\0'; }
bool gateway_config_wifi_is_valid(const gateway_config_t *config)
{ return config != NULL && config->wifi_ssid[0] != '\0'; }
bool gateway_config_mqtt_is_valid(const gateway_config_t *config)
{
    return config != NULL && config->mqtt_qos == 1 &&
           (!strncmp(config->mqtt_uri, "mqtt://", 7) || !strncmp(config->mqtt_uri, "mqtts://", 8));
}
uint32_t gateway_config_get_revision(void) { return config_revision; }

static void cfg_print(void)
{
    printf("gateway_id=%s\n", pending_config.gateway_id[0] ? pending_config.gateway_id : "<unset>");
    printf("gateway_loc=%s\n", pending_config.gateway_location[0] ? pending_config.gateway_location : "<unset>");
    printf("bcast_end_s=%lu\n", (unsigned long)(pending_config.broadcast_end_ms / 1000U));
    printf("wifi_ssid=%s\n", pending_config.wifi_ssid[0] ? pending_config.wifi_ssid : "<unset>");
    printf("wifi_password=%s\n", pending_config.wifi_password[0] ? "<hidden>" : "<unset>");
    printf("mqtt_uri=%s\n", pending_config.mqtt_uri[0] ? pending_config.mqtt_uri : "<unset>");
    printf("mqtt_username=%s\n", pending_config.mqtt_username[0] ? pending_config.mqtt_username : "<unset>");
    printf("mqtt_password=%s\n", pending_config.mqtt_password[0] ? "<hidden>" : "<unset>");
    printf("mqtt_qos=%u\n", pending_config.mqtt_qos);
    printf("ntp_server=%s\n", pending_config.ntp_server);
    printf("timezone=%s\n", pending_config.timezone);
    printf("name_rules=%s\n", pending_config.name_rules[0] ? pending_config.name_rules : "<default>");
    printf("config_ver=%lu\n", (unsigned long)pending_config.config_ver);
}

static int cfg_commit(void)
{
    nvs_handle_t handle = 0;
    esp_err_t result;
    if (pending_config.mqtt_uri[0] && !gateway_config_mqtt_is_valid(&pending_config)) {
        printf("mqtt_uri must use mqtt:// or mqtts:// and mqtt_qos must be 1\n"); return 1;
    }
    if (pending_config.name_rules[0] && !device_filter_set_rules(pending_config.name_rules)) {
        printf("name_rules must be comma-separated prefixes ending in *, e.g. SM_ICM*,SM_ICD*\n"); return 1;
    }
    result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
#define CFG_SET(call) do { if (result == ESP_OK) result = (call); } while (0)
    CFG_SET(nvs_set_str(handle, "gateway_id", pending_config.gateway_id));
    CFG_SET(nvs_set_str(handle, "gateway_loc", pending_config.gateway_location));
    CFG_SET(nvs_set_u32(handle, "bcast_end_s", pending_config.broadcast_end_ms / 1000U));
    CFG_SET(nvs_set_str(handle, "wifi_ssid", pending_config.wifi_ssid));
    CFG_SET(nvs_set_str(handle, "wifi_pwd", pending_config.wifi_password));
    CFG_SET(nvs_set_str(handle, "mqtt_uri", pending_config.mqtt_uri));
    CFG_SET(nvs_set_str(handle, "mqtt_user", pending_config.mqtt_username));
    CFG_SET(nvs_set_str(handle, "mqtt_pwd", pending_config.mqtt_password));
    CFG_SET(nvs_set_u8(handle, "mqtt_qos", pending_config.mqtt_qos));
    CFG_SET(nvs_set_str(handle, "ntp_srv", pending_config.ntp_server));
    CFG_SET(nvs_set_str(handle, "timezone", pending_config.timezone));
    CFG_SET(nvs_set_str(handle, "name_rules", pending_config.name_rules));
    pending_config.config_ver = active_config.config_ver + 1;
    CFG_SET(nvs_set_u32(handle, "config_ver", pending_config.config_ver));
    CFG_SET(nvs_commit(handle));
#undef CFG_SET
    if (handle != 0) nvs_close(handle);
    if (result != ESP_OK) { ESP_LOGE(TAG, "config commit failed: %s", esp_err_to_name(result)); return 1; }
    active_config = pending_config;
    ++config_revision;
    printf("configuration committed; network services will reload automatically\n");
    return 0;
}

static int cfg_set_string(char *destination, size_t destination_size, const char *value)
{
    if (strlen(value) >= destination_size) { printf("value too long\n"); return 1; }
    strcpy(destination, value); return 0;
}

static int cfg_command(int argc, char **argv)
{
    char *end;
    unsigned long value;
    if (argc == 2 && !strcmp(argv[1], "show")) { cfg_print(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "commit")) return cfg_commit();
    if (argc != 4 || strcmp(argv[1], "set")) goto usage;
    if (!strcmp(argv[2], "gateway_id")) return cfg_set_string(pending_config.gateway_id, sizeof(pending_config.gateway_id), argv[3]);
    if (!strcmp(argv[2], "gateway_loc")) return cfg_set_string(pending_config.gateway_location, sizeof(pending_config.gateway_location), argv[3]);
    if (!strcmp(argv[2], "wifi_ssid")) return cfg_set_string(pending_config.wifi_ssid, sizeof(pending_config.wifi_ssid), argv[3]);
    if (!strcmp(argv[2], "wifi_password")) return cfg_set_string(pending_config.wifi_password, sizeof(pending_config.wifi_password), argv[3]);
    if (!strcmp(argv[2], "mqtt_uri")) return cfg_set_string(pending_config.mqtt_uri, sizeof(pending_config.mqtt_uri), argv[3]);
    if (!strcmp(argv[2], "mqtt_username")) return cfg_set_string(pending_config.mqtt_username, sizeof(pending_config.mqtt_username), argv[3]);
    if (!strcmp(argv[2], "mqtt_password")) return cfg_set_string(pending_config.mqtt_password, sizeof(pending_config.mqtt_password), argv[3]);
    if (!strcmp(argv[2], "ntp_server")) return cfg_set_string(pending_config.ntp_server, sizeof(pending_config.ntp_server), argv[3]);
    if (!strcmp(argv[2], "timezone")) return cfg_set_string(pending_config.timezone, sizeof(pending_config.timezone), argv[3]);
    if (!strcmp(argv[2], "name_rules")) return cfg_set_string(pending_config.name_rules, sizeof(pending_config.name_rules), argv[3]);
    value = strtoul(argv[3], &end, 10);
    if (*end) goto usage;
    if (!strcmp(argv[2], "bcast_end_s") && value >= 5 && value <= 300) { pending_config.broadcast_end_ms = value * 1000U; return 0; }
    if (!strcmp(argv[2], "mqtt_qos") && value == 1) { pending_config.mqtt_qos = 1; return 0; }
usage:
    printf("usage: cfg show | cfg set <field> <value> | cfg commit\n"); return 1;
}

void gateway_config_console_start(void)
{
    static esp_console_repl_t *repl;
    const esp_console_cmd_t command = { .command = "cfg", .help = "gateway configuration", .func = &cfg_command };
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_usb_serial_jtag_config_t device_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    if (esp_console_cmd_register(&command) != ESP_OK || esp_console_register_help_command() != ESP_OK ||
        esp_console_new_repl_usb_serial_jtag(&device_config, &repl_config, &repl) != ESP_OK ||
        esp_console_start_repl(repl) != ESP_OK) {
        ESP_LOGW(TAG, "USB configuration console unavailable"); return;
    }
    ESP_LOGI(TAG, "configuration console ready: cfg show");
}
