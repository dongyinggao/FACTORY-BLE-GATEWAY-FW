#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "device_list_model.h"

static managed_device_t device_for(const char *name, uint8_t address_last, bool broadcasting, uint32_t last_seen_ms)
{
    managed_device_t device = {0};
    memcpy(device.report.name, name, strlen(name));
    device.report.address[5] = address_last;
    device.report.address_type = 1;
    device.report.rssi = -60;
    device.broadcasting = broadcasting;
    device.last_seen_ms = last_seen_ms;
    return device;
}

static void test_add_update_and_counts(void)
{
    device_list_model_t model = {0};
    managed_device_t first = device_for("SM_ICM2", 1, true, 10);
    managed_device_t update = first;

    assert(device_list_model_apply(&model, &first) == DEVICE_LIST_ADDED);
    assert(device_list_model_count(&model) == 1);
    assert(device_list_model_broadcasting_count(&model) == 1);

    update.broadcasting = false;
    update.report.rssi = -45;
    update.last_seen_ms = 20;
    assert(device_list_model_apply(&model, &update) == DEVICE_LIST_UPDATED);
    assert(device_list_model_count(&model) == 1);
    assert(device_list_model_broadcasting_count(&model) == 0);
    assert(device_list_model_get_ranked(&model, 0)->report.rssi == -45);
}

static void test_online_then_recent_order(void)
{
    device_list_model_t model = {0};
    managed_device_t offline_new = device_for("SM_ICM1", 1, false, 300);
    managed_device_t online_old = device_for("SM_ICM2", 2, true, 100);
    managed_device_t online_new = device_for("SM_ICD3", 3, true, 200);

    assert(device_list_model_apply(&model, &offline_new) == DEVICE_LIST_ADDED);
    assert(device_list_model_apply(&model, &online_old) == DEVICE_LIST_ADDED);
    assert(device_list_model_apply(&model, &online_new) == DEVICE_LIST_ADDED);
    assert(strcmp(device_list_model_get_ranked(&model, 0)->report.name, "SM_ICD3") == 0);
    assert(strcmp(device_list_model_get_ranked(&model, 1)->report.name, "SM_ICM2") == 0);
    assert(strcmp(device_list_model_get_ranked(&model, 2)->report.name, "SM_ICM1") == 0);
    assert(device_list_model_get_ranked(&model, 3) == NULL);
}

static void test_capacity_and_equal_time_devices(void)
{
    device_list_model_t model = {0};
    managed_device_t extra = device_for("SM_ICD2", 20, true, 100);

    for (uint8_t address = 0; address < DEVICE_MANAGER_MAX_DEVICES; ++address) {
        managed_device_t device = device_for("SM_ICM1", address, true, 100);
        assert(device_list_model_apply(&model, &device) == DEVICE_LIST_ADDED);
    }
    extra.report.address[4] = 1;
    assert(device_list_model_count(&model) == DEVICE_MANAGER_MAX_DEVICES);
    assert(device_list_model_get_ranked(&model, DEVICE_MANAGER_MAX_DEVICES - 1) != NULL);
    assert(device_list_model_get_ranked(&model, DEVICE_MANAGER_MAX_DEVICES) == NULL);
    assert(device_list_model_apply(&model, &extra) == DEVICE_LIST_FULL);
}

int main(void)
{
    test_add_update_and_counts();
    test_online_then_recent_order();
    test_capacity_and_equal_time_devices();
    puts("device_list_model tests passed");
    return 0;
}
