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

static void test_add_update_offline_and_online(void)
{
    device_registry_t registry = {0};
    ble_scan_report_t report = report_for("ICD001", 1, -70);
    size_t index;

    assert(device_registry_process_report(&registry, &report, 100, &index) == DEVICE_REGISTRY_ADDED);
    assert(index == 0 && registry.devices[index].online);

    report.rssi = -52;
    assert(device_registry_process_report(&registry, &report, 200, &index) == DEVICE_REGISTRY_UPDATED);
    assert(registry.devices[index].report.rssi == -52);
    assert(registry.devices[index].last_seen_ms == 200);

    assert(device_registry_mark_next_offline(&registry, 200 + DEVICE_MANAGER_OFFLINE_TIMEOUT_MS - 1, &index) ==
           DEVICE_REGISTRY_NO_CHANGE);
    assert(device_registry_mark_next_offline(&registry, 200 + DEVICE_MANAGER_OFFLINE_TIMEOUT_MS, &index) ==
           DEVICE_REGISTRY_OFFLINE);
    assert(!registry.devices[index].online);

    assert(device_registry_process_report(&registry, &report, 70000, &index) == DEVICE_REGISTRY_ONLINE);
    assert(registry.devices[index].online);
}

static void test_address_type_is_part_of_identity(void)
{
    device_registry_t registry = {0};
    ble_scan_report_t first = report_for("ICD001", 1, -60);
    ble_scan_report_t second = first;
    size_t index;

    second.address_type = 2;
    assert(device_registry_process_report(&registry, &first, 1, &index) == DEVICE_REGISTRY_ADDED);
    assert(device_registry_process_report(&registry, &second, 2, &index) == DEVICE_REGISTRY_ADDED);
    assert(index == 1);
}

static void test_full_table(void)
{
    device_registry_t registry = {0};
    size_t index;

    for (uint8_t address = 0; address < DEVICE_MANAGER_MAX_DEVICES; ++address) {
        ble_scan_report_t report = report_for("ICM_A01", address, -60);
        assert(device_registry_process_report(&registry, &report, address, &index) == DEVICE_REGISTRY_ADDED);
    }

    ble_scan_report_t extra = report_for("ICM_A02", DEVICE_MANAGER_MAX_DEVICES, -60);
    assert(device_registry_process_report(&registry, &extra, 100, &index) == DEVICE_REGISTRY_FULL);
}

static void test_optional_index_output(void)
{
    device_registry_t registry = {0};
    ble_scan_report_t report = report_for("ICD002", 42, -55);

    assert(device_registry_process_report(&registry, &report, 1, NULL) == DEVICE_REGISTRY_ADDED);
    assert(device_registry_mark_next_offline(&registry, DEVICE_MANAGER_OFFLINE_TIMEOUT_MS + 1, NULL) ==
           DEVICE_REGISTRY_OFFLINE);
}

static void test_pause_freezes_offline_timer(void)
{
    device_registry_t registry = {0};
    device_observation_clock_t clock;
    ble_scan_report_t report = report_for("ICD003", 3, -50);
    size_t index;

    device_observation_clock_init(&clock, 0);
    assert(device_registry_process_report(&registry, &report, device_observation_clock_now(&clock), &index) ==
           DEVICE_REGISTRY_ADDED);

    device_observation_clock_set_observing(&clock, true, 0);
    device_observation_clock_tick(&clock, DEVICE_MANAGER_OFFLINE_TIMEOUT_MS - 1);
    assert(device_registry_mark_next_offline(&registry, device_observation_clock_now(&clock), &index) ==
           DEVICE_REGISTRY_NO_CHANGE);

    device_observation_clock_set_observing(&clock, false, DEVICE_MANAGER_OFFLINE_TIMEOUT_MS - 1);
    device_observation_clock_tick(&clock, 120000);
    assert(device_observation_clock_now(&clock) == DEVICE_MANAGER_OFFLINE_TIMEOUT_MS - 1);
    assert(device_registry_mark_next_offline(&registry, device_observation_clock_now(&clock), &index) ==
           DEVICE_REGISTRY_NO_CHANGE);

    device_observation_clock_set_observing(&clock, true, 120000);
    device_observation_clock_tick(&clock, 120001);
    assert(device_registry_mark_next_offline(&registry, device_observation_clock_now(&clock), &index) ==
           DEVICE_REGISTRY_OFFLINE);
}

static void test_multiple_devices_and_clock_wrap(void)
{
    device_registry_t registry = {0};
    device_observation_clock_t clock;
    ble_scan_report_t first = report_for("ICD004", 4, -50);
    ble_scan_report_t second = report_for("ICM_A04", 5, -55);
    size_t index;

    assert(device_registry_process_report(&registry, &first, 0, &index) == DEVICE_REGISTRY_ADDED);
    assert(device_registry_process_report(&registry, &second, 0, &index) == DEVICE_REGISTRY_ADDED);
    assert(device_registry_mark_next_offline(&registry, DEVICE_MANAGER_OFFLINE_TIMEOUT_MS, &index) ==
           DEVICE_REGISTRY_OFFLINE);
    assert(device_registry_mark_next_offline(&registry, DEVICE_MANAGER_OFFLINE_TIMEOUT_MS, &index) ==
           DEVICE_REGISTRY_OFFLINE);
    assert(device_registry_mark_next_offline(&registry, DEVICE_MANAGER_OFFLINE_TIMEOUT_MS, &index) ==
           DEVICE_REGISTRY_NO_CHANGE);

    device_observation_clock_init(&clock, UINT32_MAX - 10U);
    device_observation_clock_set_observing(&clock, true, UINT32_MAX - 10U);
    device_observation_clock_tick(&clock, 20U);
    assert(device_observation_clock_now(&clock) == 31U);
}

int main(void)
{
    test_add_update_offline_and_online();
    test_address_type_is_part_of_identity();
    test_full_table();
    test_optional_index_output();
    test_pause_freezes_offline_timer();
    test_multiple_devices_and_clock_wrap();
    puts("device_manager tests passed");
    return 0;
}
