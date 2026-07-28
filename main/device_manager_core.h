#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ble_types.h"

#define DEVICE_MANAGER_MAX_DEVICES 128
#define DEVICE_MANAGER_DEFAULT_BCAST_END_MS 60000U
#define DEVICE_MANAGER_MIN_BCAST_END_MS 5000U
#define DEVICE_MANAGER_MAX_BCAST_END_MS 300000U

typedef struct {
    ble_scan_report_t report;
    bool broadcasting;
    uint32_t broadcast_started_ms;
    uint32_t last_seen_ms;
    uint32_t end_detected_ms;
} managed_device_t;

typedef struct {
    managed_device_t devices[DEVICE_MANAGER_MAX_DEVICES];
} device_registry_t;

typedef struct {
    bool observing;
    uint32_t observed_ms;
    uint32_t last_wall_ms;
} device_observation_clock_t;

typedef enum {
    DEVICE_REGISTRY_ADDED,
    DEVICE_REGISTRY_UPDATED,
    DEVICE_REGISTRY_BROADCAST_STARTED,
    DEVICE_REGISTRY_BROADCAST_ENDED,
    DEVICE_REGISTRY_FULL,
    DEVICE_REGISTRY_NO_CHANGE,
} device_registry_result_t;

device_registry_result_t device_registry_process_report(device_registry_t *registry,
                                                        const ble_scan_report_t *report,
                                                        uint32_t now_ms,
                                                        size_t *device_index);
device_registry_result_t device_registry_mark_next_broadcast_ended(device_registry_t *registry,
                                                                    uint32_t now_ms,
                                                                    uint32_t timeout_ms,
                                                                    size_t *device_index);
void device_observation_clock_init(device_observation_clock_t *clock, uint32_t wall_ms);
void device_observation_clock_set_observing(device_observation_clock_t *clock,
                                            bool observing,
                                            uint32_t wall_ms);
void device_observation_clock_tick(device_observation_clock_t *clock, uint32_t wall_ms);
uint32_t device_observation_clock_now(const device_observation_clock_t *clock);
