#include "gateway_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

#include "device_manager_core.h"

static const char *TAG = "gateway_config";
static const char *NVS_NAMESPACE = "gateway_cfg";
static gateway_config_t active_config = {
    .broadcast_end_ms = DEVICE_MANAGER_DEFAULT_BCAST_END_MS,
};
static gateway_config_t pending_config;

static void load_string(nvs_handle_t handle, const char *key, char *value, size_t value_size)
{
    size_t length = value_size;

    if (nvs_get_str(handle, key, value, &length) != ESP_OK) {
        value[0] = '\0';
    }
}

void gateway_config_init(void)
{
    nvs_handle_t handle = 0;
    uint32_t seconds = DEVICE_MANAGER_DEFAULT_BCAST_END_MS / 1000U;

    memset(&active_config, 0, sizeof(active_config));
    active_config.broadcast_end_ms = DEVICE_MANAGER_DEFAULT_BCAST_END_MS;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        load_string(handle, "gateway_id", active_config.gateway_id, sizeof(active_config.gateway_id));
        load_string(handle, "gateway_loc", active_config.gateway_location,
                    sizeof(active_config.gateway_location));
        if (nvs_get_u32(handle, "bcast_end_s", &seconds) == ESP_OK &&
            seconds >= DEVICE_MANAGER_MIN_BCAST_END_MS / 1000U &&
            seconds <= DEVICE_MANAGER_MAX_BCAST_END_MS / 1000U) {
            active_config.broadcast_end_ms = seconds * 1000U;
        }
        nvs_close(handle);
    }
    pending_config = active_config;
}

const gateway_config_t *gateway_config_get(void)
{
    return &active_config;
}

bool gateway_config_is_complete(void)
{
    return active_config.gateway_id[0] != '\0';
}

static void cfg_print(void)
{
    printf("gateway_id=%s\n", pending_config.gateway_id[0] ? pending_config.gateway_id : "<unset>");
    printf("gateway_loc=%s\n", pending_config.gateway_location[0] ? pending_config.gateway_location : "<unset>");
    printf("bcast_end_s=%lu\n", (unsigned long)(pending_config.broadcast_end_ms / 1000U));
}

static int cfg_commit(void)
{
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);

    if (result == ESP_OK) {
        result = nvs_set_str(handle, "gateway_id", pending_config.gateway_id);
    }
    if (result == ESP_OK) {
        result = nvs_set_str(handle, "gateway_loc", pending_config.gateway_location);
    }
    if (result == ESP_OK) {
        result = nvs_set_u32(handle, "bcast_end_s", pending_config.broadcast_end_ms / 1000U);
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "config commit failed: %s", esp_err_to_name(result));
        return 1;
    }
    active_config = pending_config;
    printf("configuration committed\n");
    return 0;
}

static int cfg_command(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "show") == 0) {
        cfg_print();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "commit") == 0) {
        return cfg_commit();
    }
    if (argc == 4 && strcmp(argv[1], "set") == 0) {
        if (strcmp(argv[2], "gateway_id") == 0) {
            if (strlen(argv[3]) >= sizeof(pending_config.gateway_id)) {
                return 1;
            }
            strcpy(pending_config.gateway_id, argv[3]);
            return 0;
        }
        if (strcmp(argv[2], "gateway_loc") == 0) {
            if (strlen(argv[3]) >= sizeof(pending_config.gateway_location)) {
                return 1;
            }
            strcpy(pending_config.gateway_location, argv[3]);
            return 0;
        }
        if (strcmp(argv[2], "bcast_end_s") == 0) {
            char *end;
            unsigned long seconds = strtoul(argv[3], &end, 10);
            if (*end != '\0' || seconds < DEVICE_MANAGER_MIN_BCAST_END_MS / 1000U ||
                seconds > DEVICE_MANAGER_MAX_BCAST_END_MS / 1000U) {
                printf("bcast_end_s must be 5..300\n");
                return 1;
            }
            pending_config.broadcast_end_ms = (uint32_t)seconds * 1000U;
            return 0;
        }
    }
    printf("usage: cfg show | cfg set <gateway_id|gateway_loc|bcast_end_s> <value> | cfg commit\n");
    return 1;
}

void gateway_config_console_start(void)
{
    static esp_console_repl_t *repl;
    const esp_console_cmd_t command = {
        .command = "cfg",
        .help = "gateway configuration: show, set, commit",
        .func = &cfg_command,
    };
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_usb_serial_jtag_config_t device_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    if (esp_console_cmd_register(&command) != ESP_OK ||
        esp_console_register_help_command() != ESP_OK ||
        esp_console_new_repl_usb_serial_jtag(&device_config, &repl_config, &repl) != ESP_OK ||
        esp_console_start_repl(repl) != ESP_OK) {
        ESP_LOGW(TAG, "USB configuration console unavailable");
        return;
    }
    ESP_LOGI(TAG, "configuration console ready: cfg show");
}
