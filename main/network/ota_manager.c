#include "ota_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_console.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#include "ble_scanner.h"
#include "device_manager.h"
#include "gateway_config.h"
#include "network_manager.h"
#include "ota_manifest_core.h"
#include "storage_manager.h"
#include "time_service.h"

#define OTA_REQUEST_QUEUE_LEN 2U
#define OTA_MANIFEST_MAX_LEN 768U
#define OTA_DOWNLOAD_BUFFER_SIZE 4096U
#define OTA_DOWNLOAD_MAX_CONSECUTIVE_RETRIES 5U
#define OTA_DOWNLOAD_RETRY_DELAY_MS 1000U
#define OTA_PREPARE_TIMEOUT_MS 5000U
#define OTA_REMOTE_SAFE_WINDOW_TIMEOUT_MS (15U * 60U * 1000U)
#define OTA_BOOT_CONFIRM_DELAY_MS 10000U
#define OTA_RELEASE_NVS_NAMESPACE "ota_release"
#define OTA_RELEASE_NVS_CONFIRMED "confirmed_seq"
#define OTA_RELEASE_NVS_PENDING "pending_seq"
#define OTA_RELEASE_NVS_PENDING_VERSION "pending_ver"
#define OTA_UAT_HTTPS_PREFIX "https://ble-gateway-uat.singularmedical.net/"

extern const uint8_t ota_uat_certificate_pem_start[]
    asm("_binary_ble_gateway_uat_ota_pem_start");

static const char *TAG = "ota_manager";

typedef enum {
    OTA_REQUEST_CHECK,
    OTA_REQUEST_START,
    OTA_REQUEST_START_ALLOW_DOWNGRADE,
} ota_request_type_t;

typedef struct {
    ota_request_type_t type;
    bool use_remote_manifest;
    char manifest_uri[GATEWAY_OTA_MANIFEST_URI_MAX_LEN];
} ota_request_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t used;
    bool overflow;
} ota_manifest_response_t;

static QueueHandle_t ota_request_queue;
static ota_manager_state_t ota_state = OTA_MANAGER_STATE_IDLE;
static ota_manifest_t ota_manifest;
static int ota_error;
static uint32_t ota_downloaded;
static uint32_t ota_confirmed_sequence;
static uint32_t ota_pending_sequence;
static uint32_t ota_last_reported_percent;
static char ota_pending_version[OTA_MANIFEST_VERSION_MAX_LEN];
static ota_manager_state_callback_t ota_state_callback;
static void *ota_state_callback_context;

static void ota_configure_tls_trust(esp_http_client_config_t *config, const char *url)
{
    if (strncmp(url, OTA_UAT_HTTPS_PREFIX, sizeof(OTA_UAT_HTTPS_PREFIX) - 1U) == 0) {
        config->cert_pem = (const char *)ota_uat_certificate_pem_start;
        config->crt_bundle_attach = NULL;
        return;
    }
    config->crt_bundle_attach = esp_crt_bundle_attach;
}

static void ota_report_download_progress(void)
{
    char progress_bar[21];
    uint32_t percent;
    size_t filled;

    if (ota_manifest.image_size == 0U) {
        return;
    }
    percent = (ota_downloaded * 100U) / ota_manifest.image_size;
    if (ota_last_reported_percent != UINT32_MAX &&
        percent < ota_last_reported_percent + 5U && percent != 100U) {
        return;
    }
    ota_last_reported_percent = percent;
    filled = (percent * 20U) / 100U;
    memset(progress_bar, '-', sizeof(progress_bar) - 1U);
    memset(progress_bar, '#', filled);
    progress_bar[sizeof(progress_bar) - 1U] = '\0';
    ESP_LOGI(TAG, "OTA [%s] %lu%% (%lu/%lu B)", progress_bar, (unsigned long)percent,
             (unsigned long)ota_downloaded, (unsigned long)ota_manifest.image_size);
    device_manager_request_ui_status_refresh();
}

static void ota_release_policy_load(void)
{
    nvs_handle_t handle = 0;

    ota_confirmed_sequence = 0;
    ota_pending_sequence = 0;
    ota_pending_version[0] = '\0';
    if (nvs_open(OTA_RELEASE_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    (void)nvs_get_u32(handle, OTA_RELEASE_NVS_CONFIRMED, &ota_confirmed_sequence);
    (void)nvs_get_u32(handle, OTA_RELEASE_NVS_PENDING, &ota_pending_sequence);
    size_t version_size = sizeof(ota_pending_version);
    if (nvs_get_str(handle, OTA_RELEASE_NVS_PENDING_VERSION, ota_pending_version, &version_size) != ESP_OK) {
        ota_pending_version[0] = '\0';
    }
    nvs_close(handle);
}

static esp_err_t ota_release_policy_write(uint32_t confirmed_sequence, uint32_t pending_sequence,
                                          const char *pending_version)
{
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(OTA_RELEASE_NVS_NAMESPACE, NVS_READWRITE, &handle);

    if (result != ESP_OK) {
        return result;
    }
    if (pending_sequence == 0U) {
        result = nvs_erase_key(handle, OTA_RELEASE_NVS_PENDING);
        if (result == ESP_ERR_NVS_NOT_FOUND) {
            result = ESP_OK;
        }
        if (result == ESP_OK) {
            result = nvs_erase_key(handle, OTA_RELEASE_NVS_PENDING_VERSION);
            if (result == ESP_ERR_NVS_NOT_FOUND) {
                result = ESP_OK;
            }
        }
    } else {
        result = nvs_set_u32(handle, OTA_RELEASE_NVS_PENDING, pending_sequence);
        if (result == ESP_OK) {
            result = nvs_set_str(handle, OTA_RELEASE_NVS_PENDING_VERSION, pending_version);
        }
    }
    if (result == ESP_OK) {
        result = nvs_set_u32(handle, OTA_RELEASE_NVS_CONFIRMED, confirmed_sequence);
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result == ESP_OK) {
        ota_confirmed_sequence = confirmed_sequence;
        ota_pending_sequence = pending_sequence;
        if (pending_sequence == 0U) {
            ota_pending_version[0] = '\0';
        } else {
            snprintf(ota_pending_version, sizeof(ota_pending_version), "%s", pending_version);
        }
    }
    return result;
}

static esp_err_t ota_release_policy_stage(const ota_manifest_t *manifest)
{
    return ota_release_policy_write(ota_confirmed_sequence, manifest->release_sequence,
                                    manifest->version);
}

static esp_err_t ota_release_policy_clear_pending(void)
{
    return ota_release_policy_write(ota_confirmed_sequence, 0U, "");
}

static esp_err_t ota_release_policy_confirm_pending(const char *running_version)
{
    uint32_t next_confirmed_sequence;

    if (ota_pending_sequence == 0U || strcmp(ota_pending_version, running_version) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    next_confirmed_sequence = ota_pending_sequence > ota_confirmed_sequence ?
                              ota_pending_sequence : ota_confirmed_sequence;
    return ota_release_policy_write(next_confirmed_sequence, 0U, "");
}

static void ota_set_state(ota_manager_state_t state, int error)
{
    ota_state = state;
    ota_error = error;
    device_manager_request_ui_status_refresh();
    if (ota_state_callback != NULL) {
        ota_state_callback(state, error, ota_state_callback_context);
    }
}

static esp_err_t ota_manifest_http_event(esp_http_client_event_t *event)
{
    ota_manifest_response_t *response = event->user_data;

    if (event->event_id != HTTP_EVENT_ON_DATA || response == NULL || event->data_len <= 0) {
        return ESP_OK;
    }
    if (response->used + (size_t)event->data_len >= response->capacity) {
        response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->buffer + response->used, event->data, (size_t)event->data_len);
    response->used += (size_t)event->data_len;
    response->buffer[response->used] = '\0';
    return ESP_OK;
}

static esp_err_t ota_fetch_manifest(const char *manifest_uri, ota_manifest_t *manifest)
{
    char response_buffer[OTA_MANIFEST_MAX_LEN] = {0};
    ota_manifest_response_t response = {
        .buffer = response_buffer,
        .capacity = sizeof(response_buffer),
    };
    esp_http_client_config_t http_config = {
        .url = manifest_uri,
        .timeout_ms = 10000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .event_handler = ota_manifest_http_event,
        .user_data = &response,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client;
    esp_err_t result;

    if (!ota_manifest_is_https_url(manifest_uri)) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_configure_tls_trust(&http_config, manifest_uri);
    client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    result = esp_http_client_perform(client);
    if (result == ESP_OK && (esp_http_client_get_status_code(client) != 200 || response.overflow ||
                             !ota_manifest_parse(response.buffer, manifest))) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    esp_http_client_cleanup(client);
    return result;
}

static esp_err_t ota_wait_for_capture_protection(bool wait_for_broadcast_end)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(wait_for_broadcast_end ?
                                                               OTA_REMOTE_SAFE_WINDOW_TIMEOUT_MS :
                                                               OTA_PREPARE_TIMEOUT_MS);

    if (!network_manager_is_connected() || !time_service_is_synced() ||
        !storage_manager_is_ready() || storage_manager_is_full()) {
        ESP_LOGW(TAG, "OTA requires connected Wi-Fi, synchronized time, and writable SD storage");
        return ESP_ERR_INVALID_STATE;
    }
    while (device_manager_broadcasting_count() != 0U) {
        if (!wait_for_broadcast_end) {
            return ESP_ERR_INVALID_STATE;
        }
        if ((int32_t)(xTaskGetTickCount() - deadline) >= 0) {
            ESP_LOGW(TAG, "OTA safe-window wait timed out with active broadcasts");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (!network_manager_is_connected() || !time_service_is_synced() ||
            !storage_manager_is_ready() || storage_manager_is_full()) {
            return ESP_ERR_INVALID_STATE;
        }
    }

    ble_scanner_stop();
    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(OTA_PREPARE_TIMEOUT_MS);
    while (ble_scanner_get_state() != BLE_SCANNER_STATE_IDLE ||
           device_manager_capture_queue_depth() != 0U || device_manager_upload_queue_depth() != 0U) {
        if ((int32_t)(xTaskGetTickCount() - deadline) >= 0) {
            ble_scanner_start();
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (!storage_manager_is_ready() || storage_manager_is_full()) {
        ble_scanner_start();
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t ota_open_image_stream(const ota_manifest_t *manifest, uint32_t offset,
                                       esp_http_client_handle_t *client_out)
{
    esp_http_client_config_t http_config = {
        .url = manifest->image_url,
        .timeout_ms = 15000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client;
    char range_header[48];
    esp_err_t result;
    int expected_status = offset == 0U ? 200 : 206;
    int expected_length = (int)(manifest->image_size - offset);

    *client_out = NULL;
    ota_configure_tls_trust(&http_config, manifest->image_url);
    client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (offset != 0U) {
        snprintf(range_header, sizeof(range_header), "bytes=%lu-", (unsigned long)offset);
        if (esp_http_client_set_header(client, "Range", range_header) != ESP_OK) {
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
    }
    result = esp_http_client_open(client, 0);
    if (result != ESP_OK || esp_http_client_fetch_headers(client) < 0 ||
        esp_http_client_get_status_code(client) != expected_status ||
        esp_http_client_get_content_length(client) != expected_length) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return result == ESP_OK ? ESP_ERR_INVALID_RESPONSE : result;
    }
    *client_out = client;
    return ESP_OK;
}

static esp_err_t ota_download_and_stage(const ota_manifest_t *manifest)
{
    const esp_partition_t *partition;
    esp_app_desc_t staged_description;
    esp_http_client_handle_t client = NULL;
    esp_ota_handle_t handle = 0;
    mbedtls_sha256_context sha_context;
    uint8_t digest[32];
    uint8_t *buffer = NULL;
    esp_err_t result = ESP_FAIL;
    int bytes_read;
    bool sha_started = false;
    uint32_t consecutive_retries = 0;

    partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL || manifest->image_size > partition->size) {
        return ESP_ERR_INVALID_SIZE;
    }
    buffer = heap_caps_malloc(OTA_DOWNLOAD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "cannot allocate OTA download buffer in PSRAM");
        return ESP_ERR_NO_MEM;
    }
    result = esp_ota_begin(partition, manifest->image_size, &handle);
    if (result != ESP_OK) {
        goto cleanup;
    }
    mbedtls_sha256_init(&sha_context);
    if (mbedtls_sha256_starts(&sha_context, 0) != 0) {
        result = ESP_FAIL;
        goto cleanup;
    }
    sha_started = true;
    ota_downloaded = 0;
    ota_last_reported_percent = UINT32_MAX;
    ota_set_state(OTA_MANAGER_STATE_DOWNLOADING, ESP_OK);
    ota_report_download_progress();
    while (ota_downloaded < manifest->image_size) {
        if (client == NULL) {
            result = ota_open_image_stream(manifest, ota_downloaded, &client);
            if (result != ESP_OK) {
                if (++consecutive_retries > OTA_DOWNLOAD_MAX_CONSECUTIVE_RETRIES) {
                    goto cleanup;
                }
                ESP_LOGW(TAG, "OTA stream open failed at %lu B (%s), retry %lu/%u",
                         (unsigned long)ota_downloaded, esp_err_to_name(result),
                         (unsigned long)consecutive_retries,
                         OTA_DOWNLOAD_MAX_CONSECUTIVE_RETRIES);
                vTaskDelay(pdMS_TO_TICKS(OTA_DOWNLOAD_RETRY_DELAY_MS));
                continue;
            }
        }
        bytes_read = esp_http_client_read(client, (char *)buffer,
                                           (int)(manifest->image_size - ota_downloaded > OTA_DOWNLOAD_BUFFER_SIZE ?
                                                 OTA_DOWNLOAD_BUFFER_SIZE : manifest->image_size - ota_downloaded));
        if (bytes_read <= 0) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            client = NULL;
            if (++consecutive_retries > OTA_DOWNLOAD_MAX_CONSECUTIVE_RETRIES) {
                result = ESP_FAIL;
                goto cleanup;
            }
            ESP_LOGW(TAG, "OTA stream interrupted at %lu B, resuming (%lu/%u)",
                     (unsigned long)ota_downloaded, (unsigned long)consecutive_retries,
                     OTA_DOWNLOAD_MAX_CONSECUTIVE_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(OTA_DOWNLOAD_RETRY_DELAY_MS));
            continue;
        }
        consecutive_retries = 0;
        if (mbedtls_sha256_update(&sha_context, buffer, (size_t)bytes_read) != 0 ||
            (result = esp_ota_write(handle, buffer, (size_t)bytes_read)) != ESP_OK) {
            result = result == ESP_OK ? ESP_FAIL : result;
            goto cleanup;
        }
        ota_downloaded += (uint32_t)bytes_read;
        ota_report_download_progress();
    }
    ota_set_state(OTA_MANAGER_STATE_VERIFYING, ESP_OK);
    if (mbedtls_sha256_finish(&sha_context, digest) != 0 ||
        !ota_manifest_sha256_matches(manifest->sha256, digest)) {
        result = ESP_ERR_INVALID_CRC;
        goto cleanup;
    }
    mbedtls_sha256_free(&sha_context);
    sha_started = false;
    result = esp_ota_end(handle);
    handle = 0;
    if (result == ESP_OK &&
        (esp_ota_get_partition_description(partition, &staged_description) != ESP_OK ||
         strcmp(staged_description.version, manifest->version) != 0)) {
        ESP_LOGE(TAG, "staged firmware version does not match Manifest (%s)", manifest->version);
        result = ESP_ERR_INVALID_VERSION;
    }
    if (result == ESP_OK) {
        result = ota_release_policy_stage(manifest);
    }
    if (result == ESP_OK) {
        result = esp_ota_set_boot_partition(partition);
    }
    if (result != ESP_OK && ota_pending_sequence == manifest->release_sequence) {
        (void)ota_release_policy_clear_pending();
    }

cleanup:
    if (sha_started) {
        mbedtls_sha256_free(&sha_context);
    }
    if (handle != 0) {
        esp_ota_abort(handle);
    }
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    heap_caps_free(buffer);
    return result;
}

static void ota_process_check(bool start_update, bool allow_downgrade, const char *manifest_uri,
                              bool wait_for_broadcast_end)
{
    esp_err_t result;
    ota_manifest_t manifest;
    const esp_app_desc_t *running = esp_app_get_description();

    ota_set_state(OTA_MANAGER_STATE_CHECKING, ESP_OK);
    result = ota_fetch_manifest(manifest_uri, &manifest);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "manifest request failed: %s", esp_err_to_name(result));
        ota_set_state(OTA_MANAGER_STATE_ERROR, result);
        return;
    }
    ota_manifest = manifest;
    if (!ota_manifest_is_compatible(&manifest)) {
        ESP_LOGE(TAG, "Manifest is not compatible with %s/%s/%s", OTA_RELEASE_HARDWARE_MODEL,
                 OTA_RELEASE_IDF_TARGET, OTA_RELEASE_PARTITION_LAYOUT);
        ota_set_state(OTA_MANAGER_STATE_ERROR, ESP_ERR_NOT_SUPPORTED);
        return;
    }
    ESP_LOGI(TAG, "manifest ready: version=%s, sequence=%lu, image_size=%lu", manifest.version,
             (unsigned long)manifest.release_sequence, (unsigned long)manifest.image_size);
    if (!start_update) {
        if (!ota_manifest_is_newer_than(&manifest, ota_confirmed_sequence) ||
            strcmp(manifest.version, running->version) == 0) {
            ESP_LOGI(TAG, "no newer OTA release: version=%s, sequence=%lu, confirmed=%lu",
                     manifest.version, (unsigned long)manifest.release_sequence,
                     (unsigned long)ota_confirmed_sequence);
            ota_set_state(OTA_MANAGER_STATE_UP_TO_DATE, ESP_OK);
        } else {
            ota_set_state(OTA_MANAGER_STATE_READY, ESP_OK);
        }
        return;
    }
    if (!allow_downgrade && !ota_manifest_is_newer_than(&manifest, ota_confirmed_sequence)) {
        ESP_LOGI(TAG, "no newer OTA release: sequence=%lu, confirmed=%lu",
                 (unsigned long)manifest.release_sequence, (unsigned long)ota_confirmed_sequence);
        ota_set_state(OTA_MANAGER_STATE_UP_TO_DATE, ESP_OK);
        return;
    }
    if (strcmp(manifest.version, running->version) == 0) {
        ESP_LOGI(TAG, "firmware version %s is already running", manifest.version);
        ota_set_state(OTA_MANAGER_STATE_UP_TO_DATE, ESP_OK);
        return;
    }
    ota_set_state(OTA_MANAGER_STATE_PREPARING, ESP_OK);
    result = ota_wait_for_capture_protection(wait_for_broadcast_end);
    if (result == ESP_OK) {
        result = ota_download_and_stage(&manifest);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s; scanning resumes", esp_err_to_name(result));
        ble_scanner_start();
        ota_set_state(OTA_MANAGER_STATE_ERROR, result);
        return;
    }
    ESP_LOGI(TAG, "OTA staged version=%s; rebooting into pending verification", manifest.version);
    ota_set_state(OTA_MANAGER_STATE_REBOOTING, ESP_OK);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void ota_manager_task(void *parameter)
{
    ota_request_t request;

    (void)parameter;
    while (true) {
        if (xQueueReceive(ota_request_queue, &request, portMAX_DELAY) == pdTRUE) {
            ota_process_check(request.type != OTA_REQUEST_CHECK,
                              request.type == OTA_REQUEST_START_ALLOW_DOWNGRADE,
                              request.use_remote_manifest ? request.manifest_uri :
                                                            gateway_config_get()->ota_manifest_uri,
                              request.use_remote_manifest);
        }
    }
}

static bool ota_request_submit(const ota_request_t *request)
{
    if (ota_request_queue == NULL || uxQueueMessagesWaiting(ota_request_queue) != 0U ||
        ota_state == OTA_MANAGER_STATE_CHECKING ||
        ota_state == OTA_MANAGER_STATE_PREPARING || ota_state == OTA_MANAGER_STATE_DOWNLOADING ||
        ota_state == OTA_MANAGER_STATE_VERIFYING || ota_state == OTA_MANAGER_STATE_REBOOTING) {
        return false;
    }
    return xQueueSend(ota_request_queue, request, 0) == pdTRUE;
}

static bool ota_request(ota_request_type_t type)
{
    const ota_request_t request = {.type = type};

    return ota_request_submit(&request);
}

bool ota_manager_request_check(void) { return ota_request(OTA_REQUEST_CHECK); }
bool ota_manager_request_start(void) { return ota_request(OTA_REQUEST_START); }
bool ota_manager_request_remote_start(const char *manifest_uri)
{
    ota_request_t request = {.type = OTA_REQUEST_START, .use_remote_manifest = true};

    if (!ota_manifest_is_https_url(manifest_uri) ||
        strlen(manifest_uri) >= sizeof(request.manifest_uri)) {
        return false;
    }
    snprintf(request.manifest_uri, sizeof(request.manifest_uri), "%s", manifest_uri);
    return ota_request_submit(&request);
}
void ota_manager_set_state_callback(ota_manager_state_callback_t callback, void *context)
{
    ota_state_callback = callback;
    ota_state_callback_context = context;
}
ota_manager_state_t ota_manager_get_state(void) { return ota_state; }
const char *ota_manager_running_version(void) { return esp_app_get_description()->version; }
const char *ota_manager_available_version(void) { return ota_manifest.version[0] ? ota_manifest.version : "<unknown>"; }
int ota_manager_last_error(void) { return ota_error; }
uint32_t ota_manager_downloaded_bytes(void) { return ota_downloaded; }
uint32_t ota_manager_image_size(void) { return ota_manifest.image_size; }
uint32_t ota_manager_confirmed_release_sequence(void) { return ota_confirmed_sequence; }
uint32_t ota_manager_pending_release_sequence(void) { return ota_pending_sequence; }
uint32_t ota_manager_available_release_sequence(void) { return ota_manifest.release_sequence; }

const char *ota_manager_status_text(void)
{
    static const char *const text[] = {
        "Idle", "Checking", "Ready", "UpToDate", "Preparing", "Downloading", "Verifying",
        "Rebooting", "Error",
    };
    return ota_state <= OTA_MANAGER_STATE_ERROR ? text[ota_state] : "Unknown";
}

static void ota_print_status(void)
{
    const esp_app_desc_t *running = esp_app_get_description();

    printf("ota_state=%s\n", ota_manager_status_text());
    printf("running_version=%s\n", running->version);
    printf("available_version=%s\n", ota_manager_available_version());
    printf("release_sequence=confirmed:%lu pending:%lu available:%lu\n",
           (unsigned long)ota_confirmed_sequence, (unsigned long)ota_pending_sequence,
           (unsigned long)ota_manifest.release_sequence);
    printf("download=%lu/%lu B\n", (unsigned long)ota_downloaded,
           (unsigned long)ota_manifest.image_size);
    printf("last_error=%d (%s)\n", ota_error, esp_err_to_name(ota_error));
}

static int ota_command(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        ota_print_status();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "check") == 0) {
        bool queued = ota_manager_request_check();

        printf(queued ? "OTA manifest check queued\n" : "OTA is busy\n");
        return queued ? 0 : 1;
    }
    if (argc == 2 && strcmp(argv[1], "start") == 0) {
        bool queued = ota_manager_request_start();

        printf(queued ? "OTA update queued; scan will stop only after safety checks\n" : "OTA is busy\n");
        return queued ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "start") == 0 &&
        strcmp(argv[2], "--allow-downgrade") == 0) {
        bool queued = ota_request(OTA_REQUEST_START_ALLOW_DOWNGRADE);

        printf(queued ? "OTA downgrade update queued; scan will stop only after safety checks\n" :
               "OTA is busy\n");
        return queued ? 0 : 1;
    }
    printf("usage: ota status | check | start [--allow-downgrade]\n");
    return 1;
}

void ota_manager_register_console(void)
{
    const esp_console_cmd_t command = {
        .command = "ota",
        .help = "HTTPS OTA maintenance: status, check, start [--allow-downgrade]",
        .func = &ota_command,
    };
    (void)esp_console_cmd_register(&command);
}

void ota_manager_start(void)
{
    if (ota_request_queue != NULL) {
        return;
    }
    ota_request_queue = xQueueCreate(OTA_REQUEST_QUEUE_LEN, sizeof(ota_request_t));
    configASSERT(ota_request_queue != NULL);
    ota_release_policy_load();
    xTaskCreate(ota_manager_task, "ota_manager", 6144, NULL, 4, NULL);
}

static void ota_confirm_task(void *parameter)
{
    esp_err_t result;

    (void)parameter;
    vTaskDelay(pdMS_TO_TICKS(OTA_BOOT_CONFIRM_DELAY_MS));
    if (ble_scanner_get_state() == BLE_SCANNER_STATE_SCANNING && storage_manager_is_ready() &&
        !storage_manager_is_full()) {
        result = esp_ota_mark_app_valid_cancel_rollback();
        if (result == ESP_OK) {
            result = ota_release_policy_confirm_pending(esp_app_get_description()->version);
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "startup self-check passed; OTA image marked valid");
                device_manager_request_ui_status_refresh();
            } else {
                ESP_LOGW(TAG, "OTA image is valid, but release sequence will be retried after restart: %s",
                         esp_err_to_name(result));
            }
            vTaskDelete(NULL);
        }
        ESP_LOGE(TAG, "cannot mark OTA image valid: %s", esp_err_to_name(result));
    } else {
        ESP_LOGE(TAG, "startup self-check failed; rebooting for OTA rollback");
    }
    esp_restart();
}

void ota_manager_confirm_running_app(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    const esp_app_desc_t *running_description = esp_app_get_description();

    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }
    if (ota_pending_sequence != 0U && strcmp(ota_pending_version, running_description->version) != 0) {
        ESP_LOGW(TAG, "discarding unbooted pending release %lu", (unsigned long)ota_pending_sequence);
        (void)ota_release_policy_clear_pending();
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        if (ota_pending_sequence != 0U &&
            strcmp(ota_pending_version, running_description->version) == 0) {
            if (ota_release_policy_confirm_pending(running_description->version) == ESP_OK) {
                ESP_LOGI(TAG, "previously verified OTA release recorded after restart");
            }
        }
        return;
    }
    if (xTaskCreate(ota_confirm_task, "ota_confirm", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create OTA confirmation task; rebooting for rollback");
        esp_restart();
    }
}
