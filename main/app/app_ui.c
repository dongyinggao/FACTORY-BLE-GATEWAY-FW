#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/m5stack_core_s3.h"
#include "lvgl.h"

#include "ble_scanner.h"
#include "csv_logger.h"
#include "device_manager.h"
#include "mqtt_service.h"
#include "network_manager.h"
#include "outbox.h"
#include "time_service.h"

#define DEVICE_LIST_VISIBLE_ROWS 4

static lv_obj_t *scanner_status_label;
static lv_obj_t *device_count_label;
static lv_obj_t *network_status_label;
static lv_obj_t *device_rows[DEVICE_LIST_VISIBLE_ROWS];
static lv_obj_t *toggle_label;
typedef struct {
    char name[BLE_DEVICE_NAME_MAX_LEN];
    uint8_t address[6];
    int8_t rssi;
    bool broadcasting;
    uint32_t sequence;
} ui_recent_device_t;

static ui_recent_device_t recent_devices[DEVICE_LIST_VISIBLE_ROWS];
static uint16_t device_count;
static uint16_t broadcasting_count;
static uint32_t device_sequence;

static const char *scanner_state_text(ble_scanner_state_t state)
{
    switch (state) {
    case BLE_SCANNER_STATE_IDLE:
        return "BLE: Idle";
    case BLE_SCANNER_STATE_SCANNING:
        return "BLE: Scanning";
    case BLE_SCANNER_STATE_STOPPING:
        return "BLE: Stopping";
    case BLE_SCANNER_STATE_ERROR:
        return "BLE: Error";
    default:
        return "BLE: Unknown";
    }
}

static const char *scanner_button_text(ble_scanner_state_t state)
{
    switch (state) {
    case BLE_SCANNER_STATE_SCANNING:
        return "Stop scan";
    case BLE_SCANNER_STATE_STOPPING:
        return "Stopping...";
    case BLE_SCANNER_STATE_IDLE:
    case BLE_SCANNER_STATE_ERROR:
    default:
        return "Start scan";
    }
}

static void update_device_list(void)
{
    lv_label_set_text_fmt(device_count_label,
                          "Devices: %u  Broadcasting: %u  SD: %s",
                          (unsigned int)device_count,
                          (unsigned int)broadcasting_count,
                          csv_logger_is_ready() ? "OK" : "ERR");

    for (size_t row = 0; row < DEVICE_LIST_VISIBLE_ROWS; ++row) {
        const ui_recent_device_t *device = &recent_devices[row];
        if (device->name[0] == '\0') {
            lv_label_set_text(device_rows[row], row == 0 ? "No matching device yet" : "");
            continue;
        }
        lv_label_set_text_fmt(device_rows[row],
                              "%s %s  %d dBm\n%02X:%02X:%02X:%02X:%02X:%02X",
                              device->broadcasting ? "START" : "END", device->name, device->rssi,
                              device->address[5], device->address[4], device->address[3],
                              device->address[2], device->address[1], device->address[0]);
    }
}

static void update_recent_device(const device_manager_ui_event_t *event)
{
    size_t selected = DEVICE_LIST_VISIBLE_ROWS;

    for (size_t index = 0; index < DEVICE_LIST_VISIBLE_ROWS; ++index) {
        if (memcmp(recent_devices[index].address, event->address, sizeof(event->address)) == 0 &&
            recent_devices[index].name[0] != '\0') {
            selected = index;
            break;
        }
        if (selected == DEVICE_LIST_VISIBLE_ROWS && recent_devices[index].name[0] == '\0') {
            selected = index;
        }
    }
    if (selected == DEVICE_LIST_VISIBLE_ROWS) {
        selected = 0;
        for (size_t index = 1; index < DEVICE_LIST_VISIBLE_ROWS; ++index) {
            if (recent_devices[index].sequence < recent_devices[selected].sequence) {
                selected = index;
            }
        }
    }
    memcpy(recent_devices[selected].name, event->name, sizeof(recent_devices[selected].name));
    memcpy(recent_devices[selected].address, event->address, sizeof(recent_devices[selected].address));
    recent_devices[selected].rssi = event->rssi;
    recent_devices[selected].broadcasting = event->broadcasting;
    recent_devices[selected].sequence = ++device_sequence;
}

static void update_network_status(void)
{
    lv_label_set_text_fmt(network_status_label, "WiFi:%s MQTT:%s OB:%lu/%luK",
                          network_manager_status_text(), mqtt_service_status_text(),
                          (unsigned long)gateway_outbox_pending_count(),
                          (unsigned long)(gateway_outbox_pending_bytes() / 1024U));
}

static void scanner_toggle_cb(lv_event_t *event)
{
    ble_scanner_state_t state;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    state = ble_scanner_get_state();
    if (state == BLE_SCANNER_STATE_SCANNING || state == BLE_SCANNER_STATE_STOPPING) {
        ble_scanner_stop();
    } else {
        ble_scanner_start();
    }
}

static void app_ui_task(void *parameter)
{
    QueueHandle_t event_queue = device_manager_get_ui_event_queue();
    device_manager_ui_event_t event;

    (void)parameter;
    while (true) {
        BaseType_t received = xQueueReceive(event_queue, &event, pdMS_TO_TICKS(1000));

        if (!bsp_display_lock(100)) {
            continue;
        }

        if (received == pdTRUE) {
            device_count = event.device_count;
            broadcasting_count = event.broadcasting_count;
        }
        if (received == pdTRUE && event.type == DEVICE_MANAGER_EVENT_SCANNER_STATE) {
            lv_label_set_text(scanner_status_label, scanner_state_text(event.scanner_state));
            lv_label_set_text(toggle_label, scanner_button_text(event.scanner_state));
            if (event.scanner_state == BLE_SCANNER_STATE_ERROR) {
                lv_label_set_text_fmt(device_count_label, "Scanner error: %d", event.error_code);
            } else {
                update_device_list();
            }
        } else {
            if (received == pdTRUE) {
                update_recent_device(&event);
            }
            update_device_list();
        }
        update_network_status();

        bsp_display_unlock();
    }
}

void app_ui_start(void)
{
    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(0);

    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "CoreS3-SE BLE Gateway");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    scanner_status_label = lv_label_create(lv_scr_act());
    lv_label_set_text(scanner_status_label, "BLE: Initializing");
    lv_obj_align(scanner_status_label, LV_ALIGN_TOP_MID, 0, 35);

    device_count_label = lv_label_create(lv_scr_act());
    lv_label_set_text(device_count_label, "Devices: 0  Broadcasting: 0  SD: Initializing");
    lv_obj_align(device_count_label, LV_ALIGN_TOP_MID, 0, 55);

    network_status_label = lv_label_create(lv_scr_act());
    lv_label_set_text(network_status_label, "WiFi:... MQTT:... OB:0/0K");
    lv_obj_align(network_status_label, LV_ALIGN_TOP_MID, 0, 73);

    for (size_t row = 0; row < DEVICE_LIST_VISIBLE_ROWS; ++row) {
        device_rows[row] = lv_label_create(lv_scr_act());
        lv_label_set_long_mode(device_rows[row], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(device_rows[row], 300);
        lv_obj_align(device_rows[row], LV_ALIGN_TOP_MID, 0, 91 + (int32_t)(row * 27));
    }
    update_device_list();
    update_network_status();

    lv_obj_t *button = lv_button_create(lv_scr_act());
    lv_obj_set_size(button, 180, 30);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_add_event_cb(button, scanner_toggle_cb, LV_EVENT_CLICKED, NULL);

    toggle_label = lv_label_create(button);
    lv_label_set_text(toggle_label, scanner_button_text(ble_scanner_get_state()));
    lv_obj_center(toggle_label);

    bsp_display_unlock();

    xTaskCreate(app_ui_task, "app_ui", 4096, NULL, 5, NULL);
}
