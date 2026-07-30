#include "ble_scanner.h"

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"
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
static bool scan_cancel_pending;
static ble_scanner_state_t scanner_state;
static uint8_t own_address_type;
static uint32_t scanner_report_drop_count;

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

static void scanner_set_state(ble_scanner_state_t state, int error_code)
{
    scanner_state = state;
    scanner_publish_state(state, error_code);
}

static void scanner_start_continuous(void);

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
        ble_scanner_event_t scanner_event = {
            .type = BLE_SCANNER_EVENT_REPORT,
            .state = BLE_SCANNER_STATE_SCANNING,
            .report = report,
        };
        if (xQueueSend(scanner_event_queue, &scanner_event, 0) != pdTRUE) {
            ++scanner_report_drop_count;
            if (scanner_report_drop_count == 1 ||
                (scanner_report_drop_count % 10U) == 0U) {
                ESP_LOGW(TAG, "scanner event queue full; reports dropped=%lu",
                         (unsigned long)scanner_report_drop_count);
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        scan_in_progress = false;
        scan_cancel_pending = false;
        ESP_LOGW(TAG, "continuous scan completed unexpectedly: reason=%d",
                 event->disc_complete.reason);
        if (scanner_enabled) {
            scanner_start_continuous();
        } else {
            scanner_set_state(BLE_SCANNER_STATE_IDLE, 0);
        }
        return 0;

    default:
        return 0;
    }
}

static void scanner_start_continuous(void)
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

    rc = ble_gap_disc(own_address_type, BLE_HS_FOREVER,
                      &parameters, scanner_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "unable to start scan: rc=%d", rc);
        scanner_set_state(BLE_SCANNER_STATE_ERROR, rc);
        return;
    }

    scan_in_progress = true;
    scanner_set_state(BLE_SCANNER_STATE_SCANNING, 0);
    ESP_LOGI(TAG, "continuous active scan started");
}

static void scanner_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "unable to infer address type: rc=%d", rc);
        scanner_set_state(BLE_SCANNER_STATE_ERROR, rc);
        return;
    }

    scanner_ready = true;
    scan_cancel_pending = false;
    ESP_LOGI(TAG, "NimBLE host synchronized");
    scanner_set_state(BLE_SCANNER_STATE_IDLE, 0);
    scanner_start_continuous();
}

static void scanner_on_reset(int reason)
{
    scan_in_progress = false;
    scan_cancel_pending = false;
    ESP_LOGW(TAG, "NimBLE reset: reason=%d", reason);
    scanner_set_state(BLE_SCANNER_STATE_ERROR, reason);
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

    scanner_event_queue = xQueueCreateWithCaps(BLE_EVENT_QUEUE_MAX_LEN,
                                                sizeof(ble_scanner_event_t),
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
    if (scanner_state == BLE_SCANNER_STATE_STOPPING) {
        ESP_LOGI(TAG, "scan stop in progress; restart will begin after cancel completes");
        return;
    }
    scanner_start_continuous();
}

void ble_scanner_stop(void)
{
    scanner_enabled = false;
    if (scanner_state == BLE_SCANNER_STATE_STOPPING || scan_cancel_pending) {
        ESP_LOGI(TAG, "scan cancellation already pending");
        return;
    }

    if (!scan_in_progress) {
        scan_cancel_pending = false;
        scanner_set_state(BLE_SCANNER_STATE_IDLE, 0);
        return;
    }

    scan_cancel_pending = true;
    scanner_set_state(BLE_SCANNER_STATE_STOPPING, 0);
    int rc = ble_gap_disc_cancel();
    if (rc == BLE_HS_EALREADY) {
        if (!ble_gap_disc_active()) {
            scan_in_progress = false;
            scan_cancel_pending = false;
            scanner_set_state(BLE_SCANNER_STATE_IDLE, 0);
        } else {
            ESP_LOGI(TAG, "scan cancellation already pending");
        }
        return;
    }

    if (rc != 0) {
        scan_cancel_pending = false;
        scanner_set_state(BLE_SCANNER_STATE_ERROR, rc);
        return;
    }

    /*
     * NimBLE clears the discovery procedure synchronously when cancellation
     * succeeds.  Unlike a time-expired session, this path does not deliver a
     * BLE_GAP_EVENT_DISC_COMPLETE callback, so waiting for that event would
     * leave the UI in STOPPING forever.
     */
    scan_in_progress = false;
    scan_cancel_pending = false;
    scanner_set_state(BLE_SCANNER_STATE_IDLE, 0);
    ESP_LOGI(TAG, "scan stopped");
}

bool ble_scanner_is_enabled(void)
{
    return scanner_enabled;
}

ble_scanner_state_t ble_scanner_get_state(void)
{
    return scanner_state;
}

QueueHandle_t ble_scanner_get_event_queue(void)
{
    return scanner_event_queue;
}

uint32_t ble_scanner_event_queue_depth(void)
{
    return scanner_event_queue == NULL ? 0U : uxQueueMessagesWaiting(scanner_event_queue);
}

uint32_t ble_scanner_report_drop_count(void)
{
    return scanner_report_drop_count;
}
