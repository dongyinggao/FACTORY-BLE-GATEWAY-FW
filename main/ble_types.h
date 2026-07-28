#pragma once

#include <stdint.h>

#include "device_filter.h"

typedef struct {
    char name[BLE_DEVICE_NAME_MAX_LEN];
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
} ble_scan_report_t;
