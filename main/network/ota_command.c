#include "ota_command.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

#include "gateway_config.h"
#include "mqtt_service.h"
#include "ota_command_core.h"
#include "ota_manager.h"
#include "time_service.h"

#define OTA_COMMAND_NVS_NAMESPACE "ota_command"
#define OTA_COMMAND_NVS_LAST_ID "last_id"

static const char *TAG = "ota_command";

typedef struct {
    bool active;
    ota_command_t command;
} ota_command_context_t;

static ota_command_context_t active_command;
static char last_command_id[OTA_COMMAND_ID_MAX_LEN];

static uint32_t ota_command_now_epoch_s(void)
{
    struct timeval now;

    gettimeofday(&now, NULL);
    return now.tv_sec > 0 ? (uint32_t)now.tv_sec : 0U;
}

static void ota_command_load_last_id(void)
{
    nvs_handle_t handle = 0;
    size_t length = sizeof(last_command_id);

    last_command_id[0] = '\0';
    if (nvs_open(OTA_COMMAND_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        if (nvs_get_str(handle, OTA_COMMAND_NVS_LAST_ID, last_command_id, &length) != ESP_OK) {
            last_command_id[0] = '\0';
        }
        nvs_close(handle);
    }
}

static bool ota_command_store_last_id(const char *command_id)
{
    nvs_handle_t handle = 0;
    esp_err_t result;

    result = nvs_open(OTA_COMMAND_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_str(handle, OTA_COMMAND_NVS_LAST_ID, command_id);
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (result == ESP_OK) {
        snprintf(last_command_id, sizeof(last_command_id), "%s", command_id);
        return true;
    }
    ESP_LOGE(TAG, "cannot persist command id: %s", esp_err_to_name(result));
    return false;
}

static const char *ota_command_state_text(ota_manager_state_t state)
{
    switch (state) {
    case OTA_MANAGER_STATE_CHECKING:
        return "checking";
    case OTA_MANAGER_STATE_READY:
        return "ready";
    case OTA_MANAGER_STATE_UP_TO_DATE:
        return "up_to_date";
    case OTA_MANAGER_STATE_PREPARING:
        return "waiting_safe_window";
    case OTA_MANAGER_STATE_DOWNLOADING:
        return "downloading";
    case OTA_MANAGER_STATE_VERIFYING:
        return "verifying";
    case OTA_MANAGER_STATE_REBOOTING:
        return "rebooting";
    case OTA_MANAGER_STATE_ERROR:
        return "failed";
    case OTA_MANAGER_STATE_IDLE:
    default:
        return "idle";
    }
}

static void ota_command_publish_status(const ota_command_t *command, const char *state, int error)
{
    char topic[160];
    char payload[384];
    const gateway_config_t *config = gateway_config_get();

    if (command == NULL || config == NULL || config->gateway_id[0] == '\0') {
        return;
    }
    snprintf(topic, sizeof(topic), "factory/product-status/gateway/%s/ota/status", config->gateway_id);
    snprintf(payload, sizeof(payload),
             "{\"message_type\":\"ota_status\",\"command_id\":\"%s\","
             "\"campaign_id\":\"%s\",\"state\":\"%s\",\"error\":%d,"
             "\"running_version\":\"%s\"}",
             command->command_id, command->campaign_id, state, error,
             ota_manager_running_version());
    if (mqtt_service_publish_to_topic(topic, payload) < 0) {
        ESP_LOGW(TAG, "OTA status not published: command=%s state=%s", command->command_id, state);
    }
}

static bool ota_command_copy_string(const cJSON *root, const char *name, char *output, size_t output_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);

    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        strlen(item->valuestring) >= output_size) {
        return false;
    }
    snprintf(output, output_size, "%s", item->valuestring);
    return true;
}

static bool ota_command_parse(const char *payload, ota_command_t *command)
{
    cJSON *root;
    const cJSON *message_type;
    const cJSON *expires_at;
    bool valid = false;

    if (payload == NULL || command == NULL) {
        return false;
    }
    root = cJSON_Parse(payload);
    if (root == NULL) {
        return false;
    }
    message_type = cJSON_GetObjectItemCaseSensitive(root, "message_type");
    expires_at = cJSON_GetObjectItemCaseSensitive(root, "expires_at_epoch_s");
    if (cJSON_IsString(message_type) && message_type->valuestring != NULL &&
        strcmp(message_type->valuestring, "ota_command") == 0 && cJSON_IsNumber(expires_at) &&
        expires_at->valuedouble > 0.0 && expires_at->valuedouble <= (double)UINT32_MAX &&
        (double)(uint32_t)expires_at->valuedouble == expires_at->valuedouble) {
        *command = (ota_command_t){0};
        command->expires_at_epoch_s = (uint32_t)expires_at->valuedouble;
        valid = ota_command_copy_string(root, "command_id", command->command_id,
                                        sizeof(command->command_id)) &&
                ota_command_copy_string(root, "campaign_id", command->campaign_id,
                                        sizeof(command->campaign_id)) &&
                ota_command_copy_string(root, "manifest_url", command->manifest_uri,
                                        sizeof(command->manifest_uri)) &&
                ota_command_is_valid(command);
    }
    cJSON_Delete(root);
    return valid;
}

static void ota_command_on_ota_state(ota_manager_state_t state, int error, void *context)
{
    (void)context;
    if (!active_command.active) {
        return;
    }
    ota_command_publish_status(&active_command.command, ota_command_state_text(state), error);
    if (state == OTA_MANAGER_STATE_ERROR || state == OTA_MANAGER_STATE_UP_TO_DATE) {
        active_command.active = false;
    }
}

static void ota_command_receive(const char *payload)
{
    ota_command_t command;
    uint32_t now_epoch_s;

    if (!ota_command_parse(payload, &command)) {
        ESP_LOGW(TAG, "ignored malformed OTA command");
        return;
    }
    if (!time_service_is_synced()) {
        ESP_LOGW(TAG, "ignored OTA command %s: time is not synchronized", command.command_id);
        return;
    }
    if (!mqtt_service_is_secure_transport()) {
        ESP_LOGW(TAG, "ignored OTA command %s: MQTT transport is not mqtts://", command.command_id);
        ota_command_publish_status(&command, "rejected_insecure_transport", ESP_ERR_NOT_SUPPORTED);
        return;
    }
    now_epoch_s = ota_command_now_epoch_s();
    if (ota_command_is_expired(&command, now_epoch_s)) {
        ESP_LOGW(TAG, "ignored expired OTA command %s", command.command_id);
        ota_command_publish_status(&command, "rejected_expired", ESP_ERR_INVALID_STATE);
        return;
    }
    if (strcmp(command.command_id, last_command_id) == 0 ||
        (active_command.active && strcmp(command.command_id, active_command.command.command_id) == 0)) {
        ESP_LOGI(TAG, "ignored duplicate OTA command %s", command.command_id);
        ota_command_publish_status(&command, "duplicate", ESP_OK);
        return;
    }
    if (!ota_manager_request_remote_start(command.manifest_uri)) {
        ESP_LOGW(TAG, "OTA command %s rejected: manager busy", command.command_id);
        ota_command_publish_status(&command, "rejected_busy", ESP_ERR_INVALID_STATE);
        return;
    }
    active_command.command = command;
    active_command.active = true;
    if (!ota_command_store_last_id(command.command_id)) {
        ESP_LOGW(TAG, "command %s accepted without persistent duplicate protection", command.command_id);
        ota_command_publish_status(&command, "accepted_volatile", ESP_FAIL);
        return;
    }
    ota_command_publish_status(&command, "accepted", ESP_OK);
    ESP_LOGI(TAG, "accepted OTA command %s for campaign %s", command.command_id, command.campaign_id);
}

void ota_command_start(void)
{
    ota_command_load_last_id();
    ota_manager_set_state_callback(ota_command_on_ota_state, NULL);
    mqtt_service_set_ota_command_handler(ota_command_receive);
}
