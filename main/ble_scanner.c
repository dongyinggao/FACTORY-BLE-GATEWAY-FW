#include "ble_scanner.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "ble_scanner";
static QueueHandle_t scanner_event_queue;
static bool scanner_enabled;
static bool scanner_ready;
static bool scan_in_progress;
static uint8_t own_address_type;

static void scanner_publish_state(ble_scanner_state_t state, int error_code)
{
    ble_scanner_event_t event = {
        .type = BLE_SCANNER_EVENT_STATE,
        .state = state,
        .error_code = error_code,
    };

    if (xQueueSend(scanner_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "scanner event queue full; state event dropped");
    }
}

static void scanner_start_session(void);

static int scanner_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_hs_adv_fields fields;
    int rc;

    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0 || fields.name == NULL || fields.name_len == 0) {
            return 0;
        }

        ble_scan_report_t report = {0};
        device_filter_copy_name(report.name, fields.name, fields.name_len);
        if (!device_filter_name_matches(report.name)) {
            return 0;
        }

        memcpy(report.address, event->disc.addr.val, sizeof(report.address));
        report.address_type = event->disc.addr.type;
        report.rssi = event->disc.rssi;
        report.adv_data_len = event->disc.length_data;
        if (report.adv_data_len > BLE_ADV_DATA_MAX_LEN) {
            report.adv_data_len = BLE_ADV_DATA_MAX_LEN;
        }
        memcpy(report.adv_data, event->disc.data, report.adv_data_len);

        ble_scanner_event_t scanner_event = {
            .type = BLE_SCANNER_EVENT_REPORT,
            .state = BLE_SCANNER_STATE_SCANNING,
            .report = report,
        };
        ESP_LOGI(TAG,
                 "matched %s addr=%02X:%02X:%02X:%02X:%02X:%02X type=%u RSSI=%d adv_len=%u",
                 report.name,
                 report.address[5], report.address[4], report.address[3],
                 report.address[2], report.address[1], report.address[0],
                 report.address_type, report.rssi, report.adv_data_len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, report.adv_data, report.adv_data_len, ESP_LOG_INFO);
        if (xQueueSend(scanner_event_queue, &scanner_event, 0) != pdTRUE) {
            ESP_LOGW(TAG, "scanner event queue full; report dropped");
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        scan_in_progress = false;
        if (scanner_enabled) {
            scanner_start_session();
        } else {
            scanner_publish_state(BLE_SCANNER_STATE_IDLE, 0);
        }
        return 0;

    default:
        return 0;
    }
}

static void scanner_start_session(void)
{
    struct ble_gap_disc_params parameters = {0};
    int rc;

    if (!scanner_ready || !scanner_enabled || scan_in_progress) {
        return;
    }

    parameters.filter_duplicates = 1;
    parameters.passive = 0;
    parameters.itvl = 0;
    parameters.window = 0;

    rc = ble_gap_disc(own_address_type, 500, &parameters, scanner_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "unable to start scan: rc=%d", rc);
        scanner_publish_state(BLE_SCANNER_STATE_ERROR, rc);
        return;
    }

    scan_in_progress = true;
    scanner_publish_state(BLE_SCANNER_STATE_SCANNING, 0);
    ESP_LOGI(TAG, "active scan started for 5 seconds");
}

static void scanner_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "unable to infer address type: rc=%d", rc);
        scanner_publish_state(BLE_SCANNER_STATE_ERROR, rc);
        return;
    }

    scanner_ready = true;
    ESP_LOGI(TAG, "NimBLE host synchronized");
    scanner_publish_state(BLE_SCANNER_STATE_IDLE, 0);
    scanner_start_session();
}

static void scanner_on_reset(int reason)
{
    scan_in_progress = false;
    ESP_LOGW(TAG, "NimBLE reset: reason=%d", reason);
    scanner_publish_state(BLE_SCANNER_STATE_ERROR, reason);
}

static void scanner_host_task(void *parameter)
{
    (void)parameter;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_scanner_init(void)
{
    int rc;

    scanner_event_queue = xQueueCreate(BLE_EVENT_QUEUE_MAX_LEN, sizeof(ble_scanner_event_t));
    configASSERT(scanner_event_queue != NULL);
    scanner_enabled = true;

    rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: rc=%d", rc);
        scanner_publish_state(BLE_SCANNER_STATE_ERROR, rc);
        return;
    }

    ble_hs_cfg.sync_cb = scanner_on_sync;
    ble_hs_cfg.reset_cb = scanner_on_reset;
    nimble_port_freertos_init(scanner_host_task);
}

void ble_scanner_start(void)
{
    scanner_enabled = true;
    scanner_start_session();
}

void ble_scanner_stop(void)
{
    scanner_enabled = false;
    if (scan_in_progress) {
        int rc = ble_gap_disc_cancel();
        if (rc != 0) {
            ESP_LOGW(TAG, "unable to cancel scan: rc=%d", rc);
        }
    } else {
        scanner_publish_state(BLE_SCANNER_STATE_IDLE, 0);
    }
}

bool ble_scanner_is_enabled(void)
{
    return scanner_enabled;
}

QueueHandle_t ble_scanner_get_event_queue(void)
{
    return scanner_event_queue;
}
