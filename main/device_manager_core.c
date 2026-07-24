#include "device_manager_core.h"

#include <string.h>

static bool registry_same_address(const managed_device_t *device, const ble_scan_report_t *report)
{
    return device->report.address_type == report->address_type &&
           memcmp(device->report.address, report->address, sizeof(report->address)) == 0;
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
    bool was_online;

    if (index < 0) {
        index = registry_find_empty_slot(registry);
        if (index < 0) {
            return DEVICE_REGISTRY_FULL;
        }
        registry->devices[index].report = *report;
        registry->devices[index].online = true;
        registry->devices[index].last_seen_ms = now_ms;
        if (device_index != NULL) {
            *device_index = (size_t)index;
        }
        return DEVICE_REGISTRY_ADDED;
    }

    was_online = registry->devices[index].online;
    registry->devices[index].report = *report;
    registry->devices[index].online = true;
    registry->devices[index].last_seen_ms = now_ms;
    if (device_index != NULL) {
        *device_index = (size_t)index;
    }
    return was_online ? DEVICE_REGISTRY_UPDATED : DEVICE_REGISTRY_ONLINE;
}

device_registry_result_t device_registry_mark_next_offline(device_registry_t *registry,
                                                            uint32_t now_ms,
                                                            size_t *device_index)
{
    for (int index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        managed_device_t *device = &registry->devices[index];
        if (device->report.name[0] != '\0' && device->online &&
            (uint32_t)(now_ms - device->last_seen_ms) >= DEVICE_MANAGER_OFFLINE_TIMEOUT_MS) {
            device->online = false;
            if (device_index != NULL) {
                *device_index = (size_t)index;
            }
            return DEVICE_REGISTRY_OFFLINE;
        }
    }
    return DEVICE_REGISTRY_NO_CHANGE;
}
