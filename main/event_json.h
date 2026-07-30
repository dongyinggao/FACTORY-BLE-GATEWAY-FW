#pragma once

#include <stddef.h>
#include <stdint.h>

#include "device_lifecycle.h"
#include "gateway_config.h"

#define GATEWAY_EVENT_ID_MAX_LEN 48
#define GATEWAY_JSON_MAX_LEN 768

typedef enum { GATEWAY_BROADCAST_STARTED, GATEWAY_BROADCAST_ENDED } gateway_broadcast_type_t;

typedef struct {
    char event_id[GATEWAY_EVENT_ID_MAX_LEN];
    gateway_broadcast_type_t type;
    device_lifecycle_event_t device;
    uint32_t event_uptime_s;
    bool time_synced;
    char timestamp[32];
    char broadcast_started_at[32];
    char last_seen_at[32];
    char end_detected_at[32];
} gateway_broadcast_message_t;

void gateway_event_id_make(char *output, size_t output_size, uint32_t boot_id, uint32_t sequence);
int gateway_json_encode_broadcast(char *output, size_t output_size,
                                  const gateway_broadcast_message_t *message,
                                  const gateway_config_t *config);
int gateway_json_encode_health(char *output, size_t output_size, const char *event_id,
                               const gateway_config_t *config, uint32_t uptime_s,
                               const char *wifi, const char *mqtt, const char *sntp,
                               bool sd_ready, uint32_t outbox_messages,
                               uint32_t capture_dropped, uint32_t upload_dropped);
