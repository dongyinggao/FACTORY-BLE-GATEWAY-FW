#include "app_ui.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/m5stack_core_s3.h"
#include "lvgl.h"

#include "ble_scanner.h"

static lv_obj_t *scanner_status_label;
static lv_obj_t *device_label;
static lv_obj_t *toggle_label;

static const char *scanner_state_text(ble_scanner_state_t state)
{
    switch (state) {
    case BLE_SCANNER_STATE_IDLE:
        return "BLE: Idle";
    case BLE_SCANNER_STATE_SCANNING:
        return "BLE: Scanning";
    case BLE_SCANNER_STATE_ERROR:
        return "BLE: Error";
    default:
        return "BLE: Unknown";
    }
}

static void update_report(const ble_scan_report_t *report)
{
    lv_label_set_text_fmt(device_label,
                          "%s\n%02X:%02X:%02X:%02X:%02X:%02X\nRSSI: %d dBm",
                          report->name,
                          report->address[5], report->address[4], report->address[3],
                          report->address[2], report->address[1], report->address[0],
                          report->rssi);
}

static void scanner_toggle_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (ble_scanner_is_enabled()) {
        ble_scanner_stop();
        lv_label_set_text(toggle_label, "Start scan");
    } else {
        ble_scanner_start();
        lv_label_set_text(toggle_label, "Stop scan");
    }
}

static void app_ui_task(void *parameter)
{
    QueueHandle_t event_queue = ble_scanner_get_event_queue();
    ble_scanner_event_t event;

    (void)parameter;
    while (true) {
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!bsp_display_lock(100)) {
            continue;
        }

        if (event.type == BLE_SCANNER_EVENT_STATE) {
            lv_label_set_text(scanner_status_label, scanner_state_text(event.state));
            if (event.state == BLE_SCANNER_STATE_ERROR) {
                lv_label_set_text_fmt(device_label, "Scanner error: %d", event.error_code);
            }
        } else if (event.type == BLE_SCANNER_EVENT_REPORT) {
            update_report(&event.report);
        }

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

    device_label = lv_label_create(lv_scr_act());
    lv_label_set_text(device_label, "No matching device yet");
    lv_obj_align(device_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *button = lv_button_create(lv_scr_act());
    lv_obj_set_size(button, 200, 48);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_add_event_cb(button, scanner_toggle_cb, LV_EVENT_CLICKED, NULL);

    toggle_label = lv_label_create(button);
    lv_label_set_text(toggle_label, "Stop scan");
    lv_obj_center(toggle_label);

    bsp_display_unlock();

    xTaskCreate(app_ui_task, "app_ui", 4096, NULL, 5, NULL);
}
