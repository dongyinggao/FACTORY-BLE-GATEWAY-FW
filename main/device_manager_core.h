#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ble_types.h"

#define DEVICE_MANAGER_MAX_DEVICES 16
#define DEVICE_MANAGER_OFFLINE_TIMEOUT_MS 60000

typedef struct {
    ble_scan_report_t report;
    bool online;
    uint32_t last_seen_ms;
} managed_device_t;

typedef struct {
    managed_device_t devices[DEVICE_MANAGER_MAX_DEVICES];
} device_registry_t;

typedef enum {
    DEVICE_REGISTRY_ADDED,
    DEVICE_REGISTRY_UPDATED,
    DEVICE_REGISTRY_ONLINE,
    DEVICE_REGISTRY_OFFLINE,
    DEVICE_REGISTRY_FULL,
    DEVICE_REGISTRY_NO_CHANGE,
} device_registry_result_t;

device_registry_result_t device_registry_process_report(device_registry_t *registry,
                                                        const ble_scan_report_t *report,
                                                        uint32_t now_ms,
                                                        size_t *device_index);
device_registry_result_t device_registry_mark_next_offline(device_registry_t *registry,
                                                            uint32_t now_ms,
                                                            size_t *device_index);
