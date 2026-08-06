#include "app_ui.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/m5stack_core_s3.h"
#include "esp_log.h"
#include "lvgl.h"

#include "ble_scanner.h"
#include "csv_logger.h"
#include "device_manager.h"
#include "mqtt_service.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "outbox.h"
#include "storage_manager.h"
#include "time_service.h"

#define DEVICE_LIST_VISIBLE_ROWS 3
#define DEVICE_LIST_ROW_HEIGHT 31
#define APP_UI_MIN_REFRESH_MS 250U

static const char *TAG = "app_ui";

static lv_obj_t *scanner_status_label;
static lv_obj_t *firmware_version_label;
static lv_obj_t *device_count_label;
static lv_obj_t *network_status_label;
static lv_obj_t *device_rows[DEVICE_LIST_VISIBLE_ROWS];
static lv_obj_t *toggle_label;
static lv_obj_t *ota_action_button;
static lv_obj_t *ota_action_label;
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
static ble_scanner_state_t displayed_scanner_state = BLE_SCANNER_STATE_IDLE;
static int displayed_scanner_error;

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
                          "Dev:%u Bcast:%u SD:%s E:%ld",
                          (unsigned int)device_count,
                          (unsigned int)broadcasting_count,
                          storage_manager_status_text(),
                          (long)storage_manager_last_error());

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

static const char *service_status_text(const char *status)
{
    if (strcmp(status, "Connected") == 0) {
        return "OK";
    }
    if (strcmp(status, "Disabled") == 0) {
        return "Off";
    }
    if (strcmp(status, "Error") == 0) {
        return "Err";
    }
    return "Wait";
}

static const char *ota_status_text(void)
{
    switch (ota_manager_get_state()) {
    case OTA_MANAGER_STATE_CHECKING:
        return "Check";
    case OTA_MANAGER_STATE_READY:
        return "Ready";
    case OTA_MANAGER_STATE_UP_TO_DATE:
        return "Current";
    case OTA_MANAGER_STATE_PREPARING:
        return "Prep";
    case OTA_MANAGER_STATE_DOWNLOADING:
        return "DL";
    case OTA_MANAGER_STATE_VERIFYING:
        return "Verify";
    case OTA_MANAGER_STATE_REBOOTING:
        return "Boot";
    case OTA_MANAGER_STATE_ERROR:
        return "Err";
    case OTA_MANAGER_STATE_IDLE:
    default:
        return "Idle";
    }
}

static const char *ota_button_text(void)
{
    switch (ota_manager_get_state()) {
    case OTA_MANAGER_STATE_READY:
        return "Start update";
    case OTA_MANAGER_STATE_UP_TO_DATE:
        return "Check update";
    case OTA_MANAGER_STATE_CHECKING:
        return "Checking...";
    case OTA_MANAGER_STATE_PREPARING:
        return "Preparing...";
    case OTA_MANAGER_STATE_DOWNLOADING:
        return "Updating...";
    case OTA_MANAGER_STATE_VERIFYING:
        return "Verifying...";
    case OTA_MANAGER_STATE_REBOOTING:
        return "Rebooting...";
    case OTA_MANAGER_STATE_ERROR:
        return "Retry check";
    case OTA_MANAGER_STATE_IDLE:
    default:
        return "Check update";
    }
}

static bool ota_button_is_busy(void)
{
    ota_manager_state_t state = ota_manager_get_state();

    return state == OTA_MANAGER_STATE_CHECKING || state == OTA_MANAGER_STATE_PREPARING ||
           state == OTA_MANAGER_STATE_DOWNLOADING || state == OTA_MANAGER_STATE_VERIFYING ||
           state == OTA_MANAGER_STATE_REBOOTING;
}

static void update_network_status(void)
{
    char ota_text[16];

    if (ota_manager_get_state() == OTA_MANAGER_STATE_DOWNLOADING && ota_manager_image_size() != 0U) {
        snprintf(ota_text, sizeof(ota_text), "DL %lu%%",
                 (unsigned long)((ota_manager_downloaded_bytes() * 100U) /
                                 ota_manager_image_size()));
    } else {
        snprintf(ota_text, sizeof(ota_text), "%s", ota_status_text());
    }
    lv_label_set_text_fmt(network_status_label, "WiFi:%s MQTT:%s Outbox:%lu OTA:%s",
                          service_status_text(network_manager_status_text()),
                          service_status_text(mqtt_service_status_text()),
                          (unsigned long)gateway_outbox_pending_count(), ota_text);
}

static void update_firmware_version(void)
{
    const char *verification = "";

    if (ota_manager_pending_release_sequence() != 0U) {
        verification = "VERIFY";
    } else if (ota_manager_confirmed_release_sequence() != 0U) {
        verification = "OK";
    }
    lv_label_set_text_fmt(firmware_version_label, "FW:%s %s", ota_manager_running_version(),
                          verification);
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

static void ota_action_cb(lv_event_t *event)
{
    bool queued;

    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (ota_manager_get_state() == OTA_MANAGER_STATE_READY) {
        queued = ota_manager_request_start();
    } else {
        queued = ota_manager_request_check();
    }
    if (!queued) {
        ESP_LOGW(TAG, "OTA request ignored because OTA is busy");
    }
}

static void app_ui_render(void)
{
    if (!bsp_display_lock(100)) {
        return;
    }

    lv_label_set_text(scanner_status_label, scanner_state_text(displayed_scanner_state));
    lv_label_set_text(toggle_label, scanner_button_text(displayed_scanner_state));
    lv_label_set_text(ota_action_label, ota_button_text());
    if (ota_button_is_busy()) {
        lv_obj_add_state(ota_action_button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(ota_action_button, LV_STATE_DISABLED);
    }
    update_firmware_version();
    if (displayed_scanner_state == BLE_SCANNER_STATE_ERROR) {
        lv_label_set_text_fmt(device_count_label, "Scanner error: %d", displayed_scanner_error);
    } else {
        update_device_list();
    }
    update_network_status();

    bsp_display_unlock();
}

static bool app_ui_apply_event(const device_manager_ui_event_t *event)
{
    if (event->type != DEVICE_MANAGER_EVENT_STATUS_CHANGED) {
        device_count = event->device_count;
        broadcasting_count = event->broadcasting_count;
    }
    if (event->type == DEVICE_MANAGER_EVENT_DEVICE_CHANGED) {
        update_recent_device(event);
    }
    if (event->type == DEVICE_MANAGER_EVENT_SCANNER_STATE) {
        displayed_scanner_state = event->scanner_state;
        displayed_scanner_error = event->error_code;
        return true;
    }
    return false;
}

static void app_ui_task(void *parameter)
{
    QueueHandle_t event_queue = device_manager_get_ui_event_queue();
    device_manager_ui_event_t event;
    TickType_t last_render_tick = xTaskGetTickCount();
    bool render_pending = false;

    (void)parameter;
    while (true) {
        TickType_t timeout = portMAX_DELAY;
        TickType_t now = xTaskGetTickCount();

        if (render_pending) {
            TickType_t elapsed = now - last_render_tick;
            timeout = elapsed >= pdMS_TO_TICKS(APP_UI_MIN_REFRESH_MS) ? 0U :
                      pdMS_TO_TICKS(APP_UI_MIN_REFRESH_MS) - elapsed;
        }

        if (xQueueReceive(event_queue, &event, timeout) != pdTRUE) {
            if (render_pending) {
                app_ui_render();
                last_render_tick = xTaskGetTickCount();
                render_pending = false;
            }
            continue;
        }

        if (app_ui_apply_event(&event)) {
            app_ui_render();
            last_render_tick = xTaskGetTickCount();
            render_pending = false;
            continue;
        }
        render_pending = true;
        if (xTaskGetTickCount() - last_render_tick >= pdMS_TO_TICKS(APP_UI_MIN_REFRESH_MS)) {
            app_ui_render();
            last_render_tick = xTaskGetTickCount();
            render_pending = false;
        }
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

    firmware_version_label = lv_label_create(lv_scr_act());
    lv_obj_set_width(firmware_version_label, 316);
    lv_label_set_long_mode(firmware_version_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(firmware_version_label, LV_ALIGN_TOP_MID, 0, 25);
    update_firmware_version();

    scanner_status_label = lv_label_create(lv_scr_act());
    displayed_scanner_state = ble_scanner_get_state();
    lv_label_set_text(scanner_status_label, scanner_state_text(displayed_scanner_state));
    lv_obj_align(scanner_status_label, LV_ALIGN_TOP_MID, 0, 43);

    device_count_label = lv_label_create(lv_scr_act());
    lv_label_set_text(device_count_label, "Dev:0 Bcast:0 SD:Retry E:-1");
    lv_obj_align(device_count_label, LV_ALIGN_TOP_MID, 0, 61);

    network_status_label = lv_label_create(lv_scr_act());
    lv_obj_set_width(network_status_label, 316);
    lv_label_set_long_mode(network_status_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(network_status_label, "WiFi:Wait MQTT:Wait Outbox:0 OTA:Idle");
    lv_obj_align(network_status_label, LV_ALIGN_TOP_MID, 0, 79);

    for (size_t row = 0; row < DEVICE_LIST_VISIBLE_ROWS; ++row) {
        device_rows[row] = lv_label_create(lv_scr_act());
        lv_label_set_long_mode(device_rows[row], LV_LABEL_LONG_CLIP);
        lv_obj_set_width(device_rows[row], 300);
        lv_obj_align(device_rows[row], LV_ALIGN_TOP_MID, 0,
                     97 + (int32_t)(row * DEVICE_LIST_ROW_HEIGHT));
    }
    update_device_list();
    update_network_status();

    lv_obj_t *button = lv_button_create(lv_scr_act());
    lv_obj_set_size(button, 145, 30);
    lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    lv_obj_add_event_cb(button, scanner_toggle_cb, LV_EVENT_CLICKED, NULL);

    toggle_label = lv_label_create(button);
    lv_label_set_text(toggle_label, scanner_button_text(displayed_scanner_state));
    lv_obj_center(toggle_label);

    ota_action_button = lv_button_create(lv_scr_act());
    lv_obj_set_size(ota_action_button, 145, 30);
    lv_obj_align(ota_action_button, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
    lv_obj_add_event_cb(ota_action_button, ota_action_cb, LV_EVENT_CLICKED, NULL);

    ota_action_label = lv_label_create(ota_action_button);
    lv_label_set_text(ota_action_label, ota_button_text());
    lv_obj_center(ota_action_label);

    bsp_display_unlock();

    xTaskCreate(app_ui_task, "app_ui", 4096, NULL, 5, NULL);
}
