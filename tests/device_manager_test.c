#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "device_manager_core.h"

static ble_scan_report_t report_for(const char *name, uint8_t address_last, int8_t rssi)
{
    ble_scan_report_t report = {0};
    memcpy(report.name, name, strlen(name));
    report.address[5] = address_last;
    report.address_type = 1;
    report.rssi = rssi;
    return report;
}

static void test_broadcast_lifecycle(void)
{
    device_registry_t registry = {0};
    ble_scan_report_t report = report_for("SM_ICM2", 1, -70);
    size_t index;

    assert(device_registry_process_report(&registry, &report, 100, 1000, &index) == DEVICE_REGISTRY_ADDED);
    assert(registry.devices[index].broadcasting);
    assert(registry.devices[index].broadcast_started_ms == 100);
    report.rssi = -52;
    assert(device_registry_process_report(&registry, &report, 200, 1100, &index) == DEVICE_REGISTRY_UPDATED);
    assert(registry.devices[index].last_seen_ms == 200);
    assert(device_registry_mark_next_broadcast_ended(&registry,
                                                      200 + DEVICE_MANAGER_DEFAULT_BCAST_END_MS - 1,
                                                      41099,
                                                      DEVICE_MANAGER_DEFAULT_BCAST_END_MS, &index) ==
           DEVICE_REGISTRY_NO_CHANGE);
    assert(device_registry_mark_next_broadcast_ended(&registry,
                                                      200 + DEVICE_MANAGER_DEFAULT_BCAST_END_MS,
                                                      41100,
                                                      DEVICE_MANAGER_DEFAULT_BCAST_END_MS, &index) ==
           DEVICE_REGISTRY_BROADCAST_ENDED);
    assert(!registry.devices[index].broadcasting);
    assert(registry.devices[index].end_detected_ms == 200 + DEVICE_MANAGER_DEFAULT_BCAST_END_MS);
    assert(device_registry_process_report(&registry, &report, 70000, 71000, &index) ==
           DEVICE_REGISTRY_BROADCAST_STARTED);
    assert(registry.devices[index].broadcast_started_ms == 70000);
}

static void test_mac_is_identity(void)
{
    device_registry_t registry = {0};
    ble_scan_report_t first = report_for("SM_ICM2", 1, -60);
    ble_scan_report_t second = first;
    size_t index;

    second.address_type = 2;
    assert(device_registry_process_report(&registry, &first, 1, 1, &index) == DEVICE_REGISTRY_ADDED);
    assert(device_registry_process_report(&registry, &second, 2, 2, &index) == DEVICE_REGISTRY_UPDATED);
    assert(index == 0);
}

static void test_128_device_capacity(void)
{
    device_registry_t registry = {0};
    size_t index;

    for (uint8_t address = 0; address < DEVICE_MANAGER_MAX_DEVICES; ++address) {
        ble_scan_report_t report = report_for("SM_ICD1", address, -60);
        assert(device_registry_process_report(&registry, &report, address, address, &index) == DEVICE_REGISTRY_ADDED);
    }
    {
        ble_scan_report_t extra = report_for("SM_ICD2", DEVICE_MANAGER_MAX_DEVICES, -60);
        assert(device_registry_process_report(&registry, &extra, 100, 100, &index) == DEVICE_REGISTRY_FULL);
    }
}

static void test_pause_freezes_timeout(void)
{
    device_registry_t registry = {0};
    device_observation_clock_t clock;
    ble_scan_report_t report = report_for("SM_ICD3", 3, -50);
    size_t index;

    device_observation_clock_init(&clock, 0);
    assert(device_registry_process_report(&registry, &report, 0, 0, &index) == DEVICE_REGISTRY_ADDED);
    device_observation_clock_set_observing(&clock, true, 0);
    device_observation_clock_tick(&clock, DEVICE_MANAGER_DEFAULT_BCAST_END_MS - 1);
    assert(device_registry_mark_next_broadcast_ended(&registry, device_observation_clock_now(&clock),
                                                      DEVICE_MANAGER_DEFAULT_BCAST_END_MS - 1,
                                                      DEVICE_MANAGER_DEFAULT_BCAST_END_MS, &index) ==
           DEVICE_REGISTRY_NO_CHANGE);
    device_observation_clock_set_observing(&clock, false, DEVICE_MANAGER_DEFAULT_BCAST_END_MS - 1);
    device_observation_clock_tick(&clock, 120000);
    assert(device_observation_clock_now(&clock) == DEVICE_MANAGER_DEFAULT_BCAST_END_MS - 1);
    device_observation_clock_set_observing(&clock, true, 120000);
    device_observation_clock_tick(&clock, 120001);
    assert(device_registry_mark_next_broadcast_ended(&registry, device_observation_clock_now(&clock),
                                                      120001,
                                                      DEVICE_MANAGER_DEFAULT_BCAST_END_MS, &index) ==
           DEVICE_REGISTRY_BROADCAST_ENDED);
}

int main(void)
{
    test_broadcast_lifecycle();
    test_mac_is_identity();
    test_128_device_capacity();
    test_pause_freezes_timeout();
    puts("device_manager tests passed");
    return 0;
}
