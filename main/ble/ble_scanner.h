#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ble_types.h"

#define BLE_EVENT_QUEUE_MAX_LEN 192

typedef enum {
    BLE_SCANNER_STATE_IDLE,
    BLE_SCANNER_STATE_SCANNING,
    BLE_SCANNER_STATE_STOPPING,
    BLE_SCANNER_STATE_ERROR,
} ble_scanner_state_t;

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
ble_scanner_state_t ble_scanner_get_state(void);
QueueHandle_t ble_scanner_get_event_queue(void);
uint32_t ble_scanner_event_queue_depth(void);
uint32_t ble_scanner_event_queue_high_watermark(void);
uint32_t ble_scanner_report_drop_count(void);

/* Diagnostic-only producer. It uses the same queue consumed by device_manager. */
bool ble_scanner_submit_diagnostic_report(const ble_scan_report_t *report);
