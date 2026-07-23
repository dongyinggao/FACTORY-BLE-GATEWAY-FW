#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "device_filter.h"

static void test_name_filter(void)
{
    assert(device_filter_name_matches("ICD001"));
    assert(device_filter_name_matches("ICM_A01"));
    assert(!device_filter_name_matches(NULL));
    assert(!device_filter_name_matches(""));
    assert(!device_filter_name_matches("IC"));
    assert(!device_filter_name_matches("ICX001"));
    assert(!device_filter_name_matches("XICD001"));
}

static void test_name_copy(void)
{
    char name[BLE_DEVICE_NAME_MAX_LEN];
    const uint8_t source[] = "ICD001";
    const uint8_t long_name[] = "ICM_abcdefghijklmnopqrstuvwxyz0123456789";

    device_filter_copy_name(name, source, sizeof(source) - 1);
    assert(strcmp(name, "ICD001") == 0);

    device_filter_copy_name(name, long_name, sizeof(long_name) - 1);
    assert(name[BLE_DEVICE_NAME_MAX_LEN - 1] == '\0');
    assert(strlen(name) == BLE_DEVICE_NAME_MAX_LEN - 1);
}

int main(void)
{
    test_name_filter();
    test_name_copy();
    puts("device_filter tests passed");
    return 0;
}
