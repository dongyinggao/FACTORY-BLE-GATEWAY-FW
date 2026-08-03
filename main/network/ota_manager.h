#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OTA_MANAGER_STATE_IDLE,
    OTA_MANAGER_STATE_CHECKING,
    OTA_MANAGER_STATE_READY,
    OTA_MANAGER_STATE_PREPARING,
    OTA_MANAGER_STATE_DOWNLOADING,
    OTA_MANAGER_STATE_VERIFYING,
    OTA_MANAGER_STATE_REBOOTING,
    OTA_MANAGER_STATE_ERROR,
} ota_manager_state_t;

typedef void (*ota_manager_state_callback_t)(ota_manager_state_t state, int error, void *context);

void ota_manager_start(void);
void ota_manager_register_console(void);
void ota_manager_confirm_running_app(void);
bool ota_manager_request_check(void);
bool ota_manager_request_start(void);
bool ota_manager_request_remote_start(const char *manifest_uri);
void ota_manager_set_state_callback(ota_manager_state_callback_t callback, void *context);
ota_manager_state_t ota_manager_get_state(void);
const char *ota_manager_status_text(void);
const char *ota_manager_available_version(void);
int ota_manager_last_error(void);
uint32_t ota_manager_downloaded_bytes(void);
uint32_t ota_manager_image_size(void);
uint32_t ota_manager_confirmed_release_sequence(void);
uint32_t ota_manager_pending_release_sequence(void);
uint32_t ota_manager_available_release_sequence(void);
const char *ota_manager_running_version(void);
