#include "app_ui.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/m5stack_core_s3.h"
#include "lvgl.h"

#include "ble_scanner.h"
#include "csv_logger.h"
#include "device_list_model.h"
#include "device_manager.h"
#include "mqtt_service.h"
#include "network_manager.h"
#include "outbox.h"
#include "time_service.h"

static lv_obj_t *scanner_status_label;
static lv_obj_t *device_count_label;
static lv_obj_t *network_status_label;
static lv_obj_t *device_rows[DEVICE_LIST_VISIBLE_ROWS];
static lv_obj_t *toggle_label;
static device_list_model_t device_list;

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
                          (unsigned int)device_list_model_count(&device_list),
                          (unsigned int)device_list_model_broadcasting_count(&device_list),
                          csv_logger_is_ready() ? "OK" : "ERR");

    for (size_t row = 0; row < DEVICE_LIST_VISIBLE_ROWS; ++row) {
        const managed_device_t *device = device_list_model_get_ranked(&device_list, row);
        if (device == NULL) {
            lv_label_set_text(device_rows[row], row == 0 ? "No matching device yet" : "");
            continue;
        }
        lv_label_set_text_fmt(device_rows[row],
                              "%s %s  %d dBm\n%02X:%02X:%02X:%02X:%02X:%02X",
                              device->broadcasting ? "ON" : "END", device->report.name, device->report.rssi,
                              device->report.address[5], device->report.address[4], device->report.address[3],
                              device->report.address[2], device->report.address[1], device->report.address[0]);
    }
}

static void update_network_status(void)
{
    lv_label_set_text_fmt(network_status_label, "WiFi:%s MQTT:%s NTP:%s Outbox:%lu",
                          network_manager_status_text(), mqtt_service_status_text(),
                          time_service_status_text(), (unsigned long)gateway_outbox_pending_count());
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
    QueueHandle_t event_queue = device_manager_get_event_queue();
    device_manager_event_t event;

    (void)parameter;
    while (true) {
        BaseType_t received = xQueueReceive(event_queue, &event, pdMS_TO_TICKS(1000));

        if (!bsp_display_lock(100)) {
            continue;
        }

        if (received == pdTRUE && event.type == DEVICE_MANAGER_EVENT_SCANNER_STATE) {
            lv_label_set_text(scanner_status_label, scanner_state_text(event.scanner_state));
            lv_label_set_text(toggle_label, scanner_button_text(event.scanner_state));
            if (event.scanner_state == BLE_SCANNER_STATE_ERROR) {
                lv_label_set_text_fmt(device_count_label, "Scanner error: %d", event.error_code);
            }
        } else if (received == pdTRUE) {
            if (device_list_model_apply(&device_list, &event.device) == DEVICE_LIST_FULL) {
                lv_label_set_text(device_count_label, "Device list full");
            } else {
                update_device_list();
            }
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
    lv_label_set_text(network_status_label, "WiFi:... MQTT:... NTP:... Outbox:0");
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
