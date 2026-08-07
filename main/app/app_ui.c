#include "app_ui.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/m5stack_core_s3.h"
#include "esp_log.h"
#include "lvgl.h"

#include "ble_scanner.h"
#include "device_manager.h"
#include "gateway_config.h"
#include "mqtt_service.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "outbox.h"
#include "storage_manager.h"
#include "time_service.h"

#define DEVICE_LIST_VISIBLE_ROWS 2
#define APP_UI_MIN_REFRESH_MS 250U
#define APP_UI_STATUS_REFRESH_MS 1000U

#define UI_COLOR_BACKGROUND 0x101820
#define UI_COLOR_HEADER 0x172633
#define UI_COLOR_CARD 0x1E303E
#define UI_COLOR_CARD_ACTIVE 0x263F50
#define UI_COLOR_TEXT 0xF3F7FA
#define UI_COLOR_MUTED 0x9FB1BD
#define UI_COLOR_GREEN 0x42D392
#define UI_COLOR_YELLOW 0xF5B942
#define UI_COLOR_RED 0xEF6B73
#define UI_COLOR_GRAY 0x778896
#define UI_COLOR_BLUE 0x48A9F8
#define UI_COLOR_PROGRESS 0x63D3FF

typedef enum {
    UI_STATUS_OK,
    UI_STATUS_WAITING,
    UI_STATUS_ERROR,
    UI_STATUS_DISABLED,
} ui_status_state_t;

typedef struct {
    lv_obj_t *card;
    lv_obj_t *value_label;
    char value[16];
    ui_status_state_t state;
    bool initialized;
} ui_status_card_t;

typedef struct {
    char name[BLE_DEVICE_NAME_MAX_LEN];
    uint8_t address[6];
    int8_t rssi;
    bool broadcasting;
    uint32_t sequence;
} ui_recent_device_t;

static const char *TAG = "app_ui";

static lv_obj_t *scanner_status_label;
static lv_obj_t *outbox_status_label;
static lv_obj_t *firmware_version_label;
static lv_obj_t *gateway_title_label;
static lv_obj_t *device_count_label;
static lv_obj_t *broadcasting_count_label;
static lv_obj_t *device_rows[DEVICE_LIST_VISIBLE_ROWS];
static lv_obj_t *toggle_button;
static lv_obj_t *toggle_label;
static lv_obj_t *ota_action_button;
static lv_obj_t *ota_action_label;
static lv_obj_t *ota_progress_bar;
static ui_status_card_t wifi_card;
static ui_status_card_t mqtt_card;
static ui_status_card_t sd_card;
static ui_status_card_t time_card;

static ui_recent_device_t recent_devices[DEVICE_LIST_VISIBLE_ROWS];
static uint16_t device_count;
static uint16_t broadcasting_count;
static uint32_t device_sequence;
static ble_scanner_state_t displayed_scanner_state = BLE_SCANNER_STATE_IDLE;
static int displayed_scanner_error;

static lv_color_t ui_status_color(ui_status_state_t state)
{
    switch (state) {
    case UI_STATUS_OK:
        return lv_color_hex(UI_COLOR_GREEN);
    case UI_STATUS_WAITING:
        return lv_color_hex(UI_COLOR_YELLOW);
    case UI_STATUS_ERROR:
        return lv_color_hex(UI_COLOR_RED);
    case UI_STATUS_DISABLED:
    default:
        return lv_color_hex(UI_COLOR_GRAY);
    }
}

static void ui_set_label_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void ui_set_label_text_fmt(lv_obj_t *label, const char *format, ...)
{
    char output[96];
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(output, sizeof(output), format, arguments);
    va_end(arguments);
    ui_set_label_text(label, output);
}

static const char *scanner_state_text(ble_scanner_state_t state)
{
    switch (state) {
    case BLE_SCANNER_STATE_IDLE:
        return "BLE idle";
    case BLE_SCANNER_STATE_SCANNING:
        return "BLE scanning";
    case BLE_SCANNER_STATE_STOPPING:
        return "BLE stopping";
    case BLE_SCANNER_STATE_ERROR:
        return "BLE error";
    default:
        return "BLE unknown";
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

static ui_status_state_t service_status_state(const char *status)
{
    if (strcmp(status, "Connected") == 0 || strcmp(status, "Synced") == 0 ||
        strcmp(status, "OK") == 0) {
        return UI_STATUS_OK;
    }
    if (strcmp(status, "Disabled") == 0) {
        return UI_STATUS_DISABLED;
    }
    if (strcmp(status, "Error") == 0 || strcmp(status, "Full") == 0) {
        return UI_STATUS_ERROR;
    }
    return UI_STATUS_WAITING;
}

static const char *service_status_short_text(const char *status)
{
    if (strcmp(status, "Connected") == 0 || strcmp(status, "Synced") == 0 ||
        strcmp(status, "OK") == 0) {
        return "OK";
    }
    if (strcmp(status, "Disabled") == 0) {
        return "Off";
    }
    if (strcmp(status, "Error") == 0 || strcmp(status, "Full") == 0) {
        return "Error";
    }
    return "Connecting";
}

static bool ota_button_is_busy(void)
{
    ota_manager_state_t state = ota_manager_get_state();

    return state == OTA_MANAGER_STATE_CHECKING || state == OTA_MANAGER_STATE_PREPARING ||
           state == OTA_MANAGER_STATE_DOWNLOADING || state == OTA_MANAGER_STATE_VERIFYING ||
           state == OTA_MANAGER_STATE_REBOOTING;
}

static void ui_update_status_card(ui_status_card_t *card, const char *value,
                                  ui_status_state_t state)
{
    if (card->initialized && card->state == state && strcmp(card->value, value) == 0) {
        return;
    }

    ui_set_label_text(card->value_label, value);
    lv_obj_set_style_text_color(card->value_label, ui_status_color(state), LV_PART_MAIN);
    lv_obj_set_style_border_color(card->card, ui_status_color(state), LV_PART_MAIN);
    snprintf(card->value, sizeof(card->value), "%s", value);
    card->state = state;
    card->initialized = true;
}

static void update_device_list(void)
{
    ui_set_label_text_fmt(device_count_label, "%u\nDevices", (unsigned int)device_count);
    ui_set_label_text_fmt(broadcasting_count_label, "%u\nBroadcasting",
                          (unsigned int)broadcasting_count);

    for (size_t row = 0; row < DEVICE_LIST_VISIBLE_ROWS; ++row) {
        const ui_recent_device_t *device = &recent_devices[row];
        if (device->name[0] == '\0') {
            ui_set_label_text(device_rows[row], row == 0 ? "No matching device yet" : "");
            lv_obj_set_style_text_color(device_rows[row], lv_color_hex(UI_COLOR_MUTED), LV_PART_MAIN);
            continue;
        }

        ui_set_label_text_fmt(device_rows[row], "%c %-11s %4d dBm  %02X:%02X:%02X:%02X:%02X:%02X",
                              device->broadcasting ? '>' : '-', device->name, device->rssi,
                              device->address[5], device->address[4], device->address[3],
                              device->address[2], device->address[1], device->address[0]);
        lv_obj_set_style_text_color(device_rows[row],
                                    device->broadcasting ? lv_color_hex(UI_COLOR_GREEN) :
                                                           lv_color_hex(UI_COLOR_TEXT),
                                    LV_PART_MAIN);
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

static void update_service_status(void)
{
    const char *wifi_status = network_manager_status_text();
    const char *mqtt_status = mqtt_service_status_text();
    const char *sd_status = storage_manager_status_text();
    const char *time_status = time_service_status_text();

    ui_update_status_card(&wifi_card, service_status_short_text(wifi_status),
                          service_status_state(wifi_status));
    ui_update_status_card(&mqtt_card, service_status_short_text(mqtt_status),
                          service_status_state(mqtt_status));
    ui_update_status_card(&sd_card, service_status_short_text(sd_status),
                          service_status_state(sd_status));
    ui_update_status_card(&time_card, service_status_short_text(time_status),
                          service_status_state(time_status));
}

static void update_outbox_status(void)
{
    const char *status = gateway_outbox_status_text();
    uint32_t pending = gateway_outbox_pending_count();
    lv_color_t color = lv_color_hex(UI_COLOR_GREEN);
    char text[32];

    if (strcmp(status, "Ready") == 0) {
        if (pending == 0U) {
            snprintf(text, sizeof(text), "Outbox clear");
        } else {
            color = lv_color_hex(UI_COLOR_YELLOW);
            snprintf(text, sizeof(text), "Outbox %lu queued", (unsigned long)pending);
        }
    } else {
        color = lv_color_hex(UI_COLOR_RED);
        snprintf(text, sizeof(text), "Outbox %s", status);
    }

    ui_set_label_text(outbox_status_label, text);
    lv_obj_set_style_text_color(outbox_status_label, color, LV_PART_MAIN);
}

static void update_firmware_version(void)
{
    const char *verification = "";

    if (ota_manager_pending_release_sequence() != 0U) {
        verification = " VERIFY";
    } else if (ota_manager_confirmed_release_sequence() != 0U) {
        verification = " OK";
    }
    ui_set_label_text_fmt(firmware_version_label, "FW %s%s", ota_manager_running_version(),
                          verification);
}

static void update_ota_action(void)
{
    ota_manager_state_t state = ota_manager_get_state();
    const char *available_version = ota_manager_available_version();
    lv_color_t surface_color = lv_color_hex(UI_COLOR_CARD);
    lv_color_t semantic_color = lv_color_hex(UI_COLOR_BLUE);
    lv_color_t label_color = semantic_color;
    char text[32];
    bool show_progress = false;

    switch (state) {
    case OTA_MANAGER_STATE_CHECKING:
        semantic_color = lv_color_hex(UI_COLOR_YELLOW);
        label_color = semantic_color;
        snprintf(text, sizeof(text), "Checking...");
        break;
    case OTA_MANAGER_STATE_READY:
        semantic_color = lv_color_hex(UI_COLOR_GREEN);
        label_color = semantic_color;
        snprintf(text, sizeof(text), "Update %s", available_version);
        break;
    case OTA_MANAGER_STATE_UP_TO_DATE:
        semantic_color = lv_color_hex(UI_COLOR_GREEN);
        label_color = semantic_color;
        snprintf(text, sizeof(text), "Latest version");
        break;
    case OTA_MANAGER_STATE_PREPARING:
        semantic_color = lv_color_hex(UI_COLOR_YELLOW);
        label_color = semantic_color;
        snprintf(text, sizeof(text), "Preparing...");
        break;
    case OTA_MANAGER_STATE_DOWNLOADING: {
        uint32_t image_size = ota_manager_image_size();
        uint32_t downloaded = ota_manager_downloaded_bytes();
        uint32_t percentage = image_size == 0U ? 0U :
                              (uint32_t)(((uint64_t)downloaded * 100U) / image_size);

        if (percentage > 100U) {
            percentage = 100U;
        }
        semantic_color = lv_color_hex(UI_COLOR_PROGRESS);
        label_color = semantic_color;
        snprintf(text, sizeof(text), "Updating %lu%%", (unsigned long)percentage);
        lv_bar_set_value(ota_progress_bar, (int32_t)percentage, LV_ANIM_OFF);
        show_progress = true;
        break;
    }
    case OTA_MANAGER_STATE_VERIFYING:
        semantic_color = lv_color_hex(UI_COLOR_YELLOW);
        label_color = semantic_color;
        snprintf(text, sizeof(text), "Verifying...");
        break;
    case OTA_MANAGER_STATE_REBOOTING:
        semantic_color = lv_color_hex(UI_COLOR_YELLOW);
        label_color = semantic_color;
        snprintf(text, sizeof(text), "Rebooting...");
        break;
    case OTA_MANAGER_STATE_ERROR:
        semantic_color = lv_color_hex(UI_COLOR_RED);
        label_color = semantic_color;
        snprintf(text, sizeof(text), "Retry: Error");
        break;
    case OTA_MANAGER_STATE_IDLE:
    default:
        surface_color = lv_color_hex(UI_COLOR_BLUE);
        semantic_color = surface_color;
        label_color = lv_color_hex(UI_COLOR_TEXT);
        snprintf(text, sizeof(text), "Check update");
        break;
    }

    ui_set_label_text(ota_action_label, text);
    lv_obj_set_style_bg_color(ota_action_button, surface_color, LV_PART_MAIN);
    lv_obj_set_style_border_color(ota_action_button, semantic_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(ota_action_button, label_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(ota_action_label, label_color, LV_PART_MAIN);
    /* A disabled control must retain its state color while OTA owns it. */
    lv_obj_set_style_bg_color(ota_action_button, surface_color,
                              LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_border_color(ota_action_button, semantic_color,
                                  LV_PART_MAIN | LV_STATE_DISABLED);
    if (show_progress) {
        lv_obj_clear_flag(ota_progress_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ota_progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
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

    ui_set_label_text(scanner_status_label, scanner_state_text(displayed_scanner_state));
    ui_set_label_text(toggle_label, scanner_button_text(displayed_scanner_state));
    update_ota_action();
    if (ota_button_is_busy()) {
        lv_obj_add_state(ota_action_button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(ota_action_button, LV_STATE_DISABLED);
    }
    lv_obj_set_style_bg_color(toggle_button,
                              displayed_scanner_state == BLE_SCANNER_STATE_SCANNING ?
                                  lv_color_hex(UI_COLOR_RED) : lv_color_hex(UI_COLOR_BLUE),
                              LV_PART_MAIN);
    update_firmware_version();
    update_service_status();
    update_outbox_status();
    if (displayed_scanner_state == BLE_SCANNER_STATE_ERROR) {
        ui_set_label_text_fmt(device_count_label, "Error\n%d", displayed_scanner_error);
    } else {
        update_device_list();
    }

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
    TickType_t last_status_tick = last_render_tick;
    bool render_pending = false;

    (void)parameter;
    while (true) {
        TickType_t timeout = pdMS_TO_TICKS(APP_UI_STATUS_REFRESH_MS);
        TickType_t now = xTaskGetTickCount();

        if (render_pending) {
            TickType_t elapsed = now - last_render_tick;
            timeout = elapsed >= pdMS_TO_TICKS(APP_UI_MIN_REFRESH_MS) ? 0U :
                      pdMS_TO_TICKS(APP_UI_MIN_REFRESH_MS) - elapsed;
        }

        if (xQueueReceive(event_queue, &event, timeout) == pdTRUE) {
            if (app_ui_apply_event(&event)) {
                app_ui_render();
                last_render_tick = xTaskGetTickCount();
                last_status_tick = last_render_tick;
                render_pending = false;
                continue;
            }
            render_pending = true;
        }

        now = xTaskGetTickCount();
        if (render_pending && now - last_render_tick >= pdMS_TO_TICKS(APP_UI_MIN_REFRESH_MS)) {
            app_ui_render();
            last_render_tick = xTaskGetTickCount();
            last_status_tick = last_render_tick;
            render_pending = false;
        } else if (now - last_status_tick >= pdMS_TO_TICKS(APP_UI_STATUS_REFRESH_MS)) {
            app_ui_render();
            last_render_tick = xTaskGetTickCount();
            last_status_tick = last_render_tick;
        }
    }
}

static void app_ui_style_card(lv_obj_t *card, int radius)
{
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_CARD_ACTIVE), LV_PART_MAIN);
    lv_obj_set_style_radius(card, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 4, LV_PART_MAIN);
}

static void app_ui_create_status_card(ui_status_card_t *status_card, const char *title,
                                      int32_t x_offset)
{
    lv_obj_t *title_label;

    status_card->card = lv_obj_create(lv_scr_act());
    lv_obj_set_size(status_card->card, 72, 39);
    lv_obj_align(status_card->card, LV_ALIGN_TOP_LEFT, x_offset, 38);
    app_ui_style_card(status_card->card, 7);

    title_label = lv_label_create(status_card->card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(UI_COLOR_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    status_card->value_label = lv_label_create(status_card->card);
    lv_obj_set_style_text_font(status_card->value_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(status_card->value_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static lv_obj_t *app_ui_create_metric_card(int32_t x_offset, const char *initial_text)
{
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    lv_obj_t *label;

    lv_obj_set_size(card, 144, 33);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x_offset, 82);
    app_ui_style_card(card, 8);
    label = lv_label_create(card);
    lv_label_set_text(label, initial_text);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(label);
    return label;
}

void app_ui_start(void)
{
    const gateway_config_t *config;
    lv_obj_t *header;

    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(0);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(UI_COLOR_BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, LV_PART_MAIN);

    header = lv_obj_create(lv_scr_act());
    lv_obj_set_size(header, 320, 33);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_HEADER), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN);

    config = gateway_config_get();
    gateway_title_label = lv_label_create(header);
    ui_set_label_text_fmt(gateway_title_label, "BLE Gateway  %s",
                          config->gateway_id[0] == '\0' ? "Unconfigured" : config->gateway_id);
    lv_obj_set_style_text_color(gateway_title_label, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(gateway_title_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(gateway_title_label, LV_ALIGN_LEFT_MID, 7, 0);

    firmware_version_label = lv_label_create(header);
    lv_obj_set_width(firmware_version_label, 120);
    lv_label_set_long_mode(firmware_version_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(firmware_version_label, lv_color_hex(UI_COLOR_GREEN), LV_PART_MAIN);
    lv_obj_set_style_text_align(firmware_version_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(firmware_version_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(firmware_version_label, LV_ALIGN_RIGHT_MID, -7, 0);

    app_ui_create_status_card(&wifi_card, "WiFi", 8);
    app_ui_create_status_card(&mqtt_card, "MQTT", 86);
    app_ui_create_status_card(&sd_card, "SD", 164);
    app_ui_create_status_card(&time_card, "Time", 242);

    device_count_label = app_ui_create_metric_card(8, "0\nDevices");
    broadcasting_count_label = app_ui_create_metric_card(168, "0\nBroadcasting");

    scanner_status_label = lv_label_create(lv_scr_act());
    displayed_scanner_state = ble_scanner_get_state();
    lv_obj_set_style_text_color(scanner_status_label, lv_color_hex(UI_COLOR_BLUE), LV_PART_MAIN);
    lv_obj_set_style_text_font(scanner_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(scanner_status_label, LV_ALIGN_TOP_LEFT, 9, 120);

    outbox_status_label = lv_label_create(lv_scr_act());
    lv_obj_set_width(outbox_status_label, 178);
    lv_label_set_long_mode(outbox_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(outbox_status_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_font(outbox_status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(outbox_status_label, LV_ALIGN_TOP_RIGHT, -8, 120);

    for (size_t row = 0; row < DEVICE_LIST_VISIBLE_ROWS; ++row) {
        lv_obj_t *row_card = lv_obj_create(lv_scr_act());
        device_rows[row] = lv_label_create(row_card);
        lv_obj_set_size(row_card, 304, 25);
        lv_obj_align(row_card, LV_ALIGN_TOP_MID, 0, 139 + (int32_t)(row * 28));
        app_ui_style_card(row_card, 5);
        lv_obj_set_style_pad_left(row_card, 5, LV_PART_MAIN);
        lv_obj_set_style_pad_top(row_card, 1, LV_PART_MAIN);
        lv_obj_set_width(device_rows[row], 292);
        lv_label_set_long_mode(device_rows[row], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(device_rows[row], &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(device_rows[row], LV_ALIGN_LEFT_MID, 0, 0);
    }

    toggle_button = lv_button_create(lv_scr_act());
    lv_obj_set_size(toggle_button, 145, 31);
    lv_obj_align(toggle_button, LV_ALIGN_BOTTOM_LEFT, 8, -5);
    lv_obj_set_style_radius(toggle_button, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(toggle_button, scanner_toggle_cb, LV_EVENT_CLICKED, NULL);
    toggle_label = lv_label_create(toggle_button);
    lv_obj_set_style_text_color(toggle_label, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(toggle_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(toggle_label);

    ota_action_button = lv_button_create(lv_scr_act());
    lv_obj_set_size(ota_action_button, 145, 31);
    lv_obj_align(ota_action_button, LV_ALIGN_BOTTOM_RIGHT, -8, -5);
    lv_obj_set_style_bg_color(ota_action_button, lv_color_hex(UI_COLOR_CARD), LV_PART_MAIN);
    lv_obj_set_style_border_width(ota_action_button, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(ota_action_button, lv_color_hex(UI_COLOR_BLUE), LV_PART_MAIN);
    lv_obj_set_style_radius(ota_action_button, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(ota_action_button, ota_action_cb, LV_EVENT_CLICKED, NULL);
    ota_action_label = lv_label_create(ota_action_button);
    lv_obj_set_style_text_color(ota_action_label, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ota_action_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(ota_action_label, LV_ALIGN_BOTTOM_MID, 0, -2);

    ota_progress_bar = lv_bar_create(ota_action_button);
    lv_obj_set_size(ota_progress_bar, 129, 3);
    lv_obj_align(ota_progress_bar, LV_ALIGN_TOP_MID, 0, 4);
    lv_bar_set_range(ota_progress_bar, 0, 100);
    lv_obj_set_style_bg_color(ota_progress_bar, lv_color_hex(UI_COLOR_BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ota_progress_bar, lv_color_hex(UI_COLOR_PROGRESS), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ota_progress_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(ota_progress_bar, 2, LV_PART_INDICATOR);
    lv_obj_add_flag(ota_progress_bar, LV_OBJ_FLAG_HIDDEN);

    bsp_display_unlock();

    app_ui_render();

    xTaskCreate(app_ui_task, "app_ui", 4096, NULL, 5, NULL);
}
