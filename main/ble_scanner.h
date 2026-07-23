#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "device_filter.h"

#define BLE_ADV_DATA_MAX_LEN 31
#define BLE_EVENT_QUEUE_MAX_LEN 12

typedef enum {
    BLE_SCANNER_STATE_IDLE,
    BLE_SCANNER_STATE_SCANNING,
    BLE_SCANNER_STATE_ERROR,
} ble_scanner_state_t;

typedef struct {
    char name[BLE_DEVICE_NAME_MAX_LEN];
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    uint8_t adv_data_len;
    uint8_t adv_data[BLE_ADV_DATA_MAX_LEN];
} ble_scan_report_t;

typedef enum {
    BLE_SCANNER_EVENT_STATE,
    BLE_SCANNER_EVENT_REPORT,
} ble_scanner_event_type_t;

typedef struct {
    ble_scanner_event_type_t type;
    ble_scanner_state_t state;
    int error_code;
    ble_scan_report_t report;
} ble_scanner_event_t;

void ble_scanner_init(void);
void ble_scanner_start(void);
void ble_scanner_stop(void);
bool ble_scanner_is_enabled(void);
QueueHandle_t ble_scanner_get_event_queue(void);
