#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ble_scanner.h"
#include "device_manager_core.h"
#include "device_lifecycle.h"

typedef enum {
    DEVICE_MANAGER_EVENT_SCANNER_STATE,
    DEVICE_MANAGER_EVENT_DEVICE_CHANGED,
    DEVICE_MANAGER_EVENT_STATUS_CHANGED,
} device_manager_event_type_t;

typedef struct {
    device_manager_event_type_t type;
    ble_scanner_state_t scanner_state;
    int error_code;
    uint16_t device_count;
    uint16_t broadcasting_count;
    char name[BLE_DEVICE_NAME_MAX_LEN];
    uint8_t address[6];
    int8_t rssi;
    bool broadcasting;
} device_manager_ui_event_t;

#define DEVICE_MANAGER_UI_QUEUE_LEN 32
#define DEVICE_MANAGER_LIFECYCLE_QUEUE_LEN 512
#define DEVICE_MANAGER_ACTIVITY_QUEUE_LEN 64

void device_manager_init(void);
QueueHandle_t device_manager_get_ui_event_queue(void);
void device_manager_request_ui_status_refresh(void);
QueueHandle_t device_manager_get_capture_queue(void);
QueueHandle_t device_manager_get_upload_queue(void);
QueueHandle_t device_manager_get_activity_queue(void);
uint32_t device_manager_capture_drop_count(void);
uint32_t device_manager_upload_drop_count(void);
uint32_t device_manager_activity_drop_count(void);
uint32_t device_manager_table_reject_count(void);
uint32_t device_manager_ui_drop_count(void);
uint16_t device_manager_registered_count(void);
uint16_t device_manager_broadcasting_count(void);
uint32_t device_manager_ui_queue_depth(void);
uint32_t device_manager_capture_queue_depth(void);
uint32_t device_manager_upload_queue_depth(void);
uint32_t device_manager_ui_queue_high_watermark(void);
uint32_t device_manager_capture_queue_high_watermark(void);
uint32_t device_manager_upload_queue_high_watermark(void);
