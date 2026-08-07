#pragma once

#include <stdint.h>

#include "ble_types.h"

typedef enum {
    DEVICE_LIFECYCLE_BROADCAST_STARTED,
    DEVICE_LIFECYCLE_BROADCAST_ACTIVE,
    DEVICE_LIFECYCLE_BROADCAST_ENDED,
} device_lifecycle_event_type_t;

/* Compact, immutable event for CSV and MQTT. It deliberately excludes raw
 * advertising payloads and UI-only state so high-volume queues fit in PSRAM. */
typedef struct {
    device_lifecycle_event_type_t type;
    char broadcast_id[24];
    char name[BLE_DEVICE_NAME_MAX_LEN];
    uint8_t address[6];
    int8_t rssi;
    uint32_t broadcast_started_ms;
    uint32_t last_seen_ms;
    uint32_t end_detected_ms;
    uint64_t broadcast_started_wall_ms;
    uint64_t last_seen_wall_ms;
    uint64_t end_detected_wall_ms;
} device_lifecycle_event_t;
