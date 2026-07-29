#include "device_manager_core.h"

#include <string.h>

static bool registry_same_address(const managed_device_t *device, const ble_scan_report_t *report)
{
    return memcmp(device->report.address, report->address, sizeof(report->address)) == 0;
}

static int registry_find_device(const device_registry_t *registry, const ble_scan_report_t *report)
{
    for (int index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        if (registry->devices[index].report.name[0] != '\0' &&
            registry_same_address(&registry->devices[index], report)) {
            return index;
        }
    }
    return -1;
}

static int registry_find_empty_slot(const device_registry_t *registry)
{
    for (int index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        if (registry->devices[index].report.name[0] == '\0') {
            return index;
        }
    }
    return -1;
}

device_registry_result_t device_registry_process_report(device_registry_t *registry,
                                                        const ble_scan_report_t *report,
                                                        uint32_t now_ms,
                                                        size_t *device_index)
{
    int index = registry_find_device(registry, report);
    bool was_broadcasting;

    if (index < 0) {
        index = registry_find_empty_slot(registry);
        if (index < 0) {
            return DEVICE_REGISTRY_FULL;
        }
        registry->devices[index].report = *report;
        registry->devices[index].broadcasting = true;
        registry->devices[index].broadcast_started_ms = now_ms;
        registry->devices[index].last_seen_ms = now_ms;
        if (device_index != NULL) {
            *device_index = (size_t)index;
        }
        return DEVICE_REGISTRY_ADDED;
    }

    was_broadcasting = registry->devices[index].broadcasting;
    registry->devices[index].report = *report;
    registry->devices[index].broadcasting = true;
    if (!was_broadcasting) {
        registry->devices[index].broadcast_started_ms = now_ms;
        registry->devices[index].end_detected_ms = 0;
    }
    registry->devices[index].last_seen_ms = now_ms;
    if (device_index != NULL) {
        *device_index = (size_t)index;
    }
    return was_broadcasting ? DEVICE_REGISTRY_UPDATED : DEVICE_REGISTRY_BROADCAST_STARTED;
}

device_registry_result_t device_registry_mark_next_broadcast_ended(device_registry_t *registry,
                                                                    uint32_t now_ms,
                                                                    uint32_t timeout_ms,
                                                                    size_t *device_index)
{
    for (int index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        managed_device_t *device = &registry->devices[index];
        if (device->report.name[0] != '\0' && device->broadcasting &&
            (uint32_t)(now_ms - device->last_seen_ms) >= timeout_ms) {
            device->broadcasting = false;
            device->end_detected_ms = now_ms;
            if (device_index != NULL) {
                *device_index = (size_t)index;
            }
            return DEVICE_REGISTRY_BROADCAST_ENDED;
        }
    }
    return DEVICE_REGISTRY_NO_CHANGE;
}

void device_observation_clock_init(device_observation_clock_t *clock, uint32_t wall_ms)
{
    clock->observing = false;
    clock->observed_ms = 0;
    clock->last_wall_ms = wall_ms;
}

void device_observation_clock_tick(device_observation_clock_t *clock, uint32_t wall_ms)
{
    if (clock->observing) {
        clock->observed_ms += (uint32_t)(wall_ms - clock->last_wall_ms);
    }
    clock->last_wall_ms = wall_ms;
}

void device_observation_clock_set_observing(device_observation_clock_t *clock,
                                            bool observing,
                                            uint32_t wall_ms)
{
    device_observation_clock_tick(clock, wall_ms);
    clock->observing = observing;
}

uint32_t device_observation_clock_now(const device_observation_clock_t *clock)
{
    return clock->observed_ms;
}
