#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "device_filter.h"

static void test_name_filter(void)
{
    assert(device_filter_name_matches("SM_ICM2"));
    assert(device_filter_name_matches("SM_iCM2"));
    assert(device_filter_name_matches("sm_icd123"));
    assert(!device_filter_name_matches(NULL));
    assert(!device_filter_name_matches(""));
    assert(!device_filter_name_matches("SM_ICM"));
    assert(!device_filter_name_matches("SM_ICM_A01"));
    assert(!device_filter_name_matches("SM_ICX001"));
    assert(!device_filter_name_matches("XSM_ICM2"));
    assert(!device_filter_name_matches("SM_ICM2X"));
}

static void test_configured_rules_and_fallback(void)
{
    assert(device_filter_set_rules("SM_TEST*"));
    assert(device_filter_name_matches("sm_test42"));
    assert(!device_filter_name_matches("SM_ICM2"));
    assert(!device_filter_set_rules("SM_TEST"));
    assert(device_filter_name_matches("SM_ICM2"));
    assert(device_filter_name_matches("SM_ICD8"));
}

static void test_name_copy(void)
{
    char name[BLE_DEVICE_NAME_MAX_LEN];
    const uint8_t source[] = "SM_ICM2";
    const uint8_t long_name[] = "SM_ICMabcdefghijklmnopqrstuvwxyz0123456789";

    device_filter_copy_name(name, source, sizeof(source) - 1);
    assert(strcmp(name, "SM_ICM2") == 0);

    device_filter_copy_name(name, long_name, sizeof(long_name) - 1);
    assert(name[BLE_DEVICE_NAME_MAX_LEN - 1] == '\0');
    assert(strlen(name) == BLE_DEVICE_NAME_MAX_LEN - 1);
}

int main(void)
{
    test_name_filter();
    test_configured_rules_and_fallback();
    test_name_copy();
    puts("device_filter tests passed");
    return 0;
}
