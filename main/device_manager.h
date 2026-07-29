#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ble_scanner.h"
#include "device_manager_core.h"

typedef enum {
    DEVICE_MANAGER_EVENT_SCANNER_STATE,
    DEVICE_MANAGER_EVENT_DEVICE_ADDED,
    DEVICE_MANAGER_EVENT_DEVICE_UPDATED,
    DEVICE_MANAGER_EVENT_BROADCAST_STARTED,
    DEVICE_MANAGER_EVENT_BROADCAST_ENDED,
} device_manager_event_type_t;

typedef struct {
    device_manager_event_type_t type;
    ble_scanner_state_t scanner_state;
    int error_code;
    managed_device_t device;
} device_manager_event_t;

void device_manager_init(void);
QueueHandle_t device_manager_get_event_queue(void);
QueueHandle_t device_manager_get_capture_queue(void);
