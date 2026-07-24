#pragma once

#include <stdint.h>

#include "device_filter.h"

#define BLE_ADV_DATA_MAX_LEN 31

typedef struct {
    char name[BLE_DEVICE_NAME_MAX_LEN];
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    uint8_t adv_data_len;
    uint8_t adv_data[BLE_ADV_DATA_MAX_LEN];
} ble_scan_report_t;
