#pragma once

#include <stdbool.h>
#include <stdint.h>

#define OTA_COMMAND_ID_MAX_LEN 48
#define OTA_COMMAND_CAMPAIGN_ID_MAX_LEN 48
#define OTA_COMMAND_MANIFEST_URI_MAX_LEN 192

typedef struct {
    char command_id[OTA_COMMAND_ID_MAX_LEN];
    char campaign_id[OTA_COMMAND_CAMPAIGN_ID_MAX_LEN];
    char manifest_uri[OTA_COMMAND_MANIFEST_URI_MAX_LEN];
    uint32_t expires_at_epoch_s;
} ota_command_t;

bool ota_command_is_valid(const ota_command_t *command);
bool ota_command_is_expired(const ota_command_t *command, uint32_t now_epoch_s);
