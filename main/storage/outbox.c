#include "outbox.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bsp/m5stack_core_s3.h"
#include "esp_log.h"

#include "outbox_core.h"
#include "storage_manager.h"
#include "device_manager.h"

#define OUTBOX_DIR BSP_SD_MOUNT_POINT "/outbox"
#define OUTBOX_SEGMENT_BYTES (256U * 1024U)
#define OUTBOX_MAX_BYTES (256U * 1024U * 1024U)
#define OUTBOX_HEALTH_FILE OUTBOX_DIR "/HEALTH.LOG"
#define OUTBOX_HEALTH_NEXT_FILE OUTBOX_DIR "/HEALTH.NXT"

static const char *TAG = "outbox";
static bool ready;
static char read_path[64];
static long read_offset;
static long current_next_offset;
static bool current_inflight;
static bool current_health;
static gateway_outbox_core_t core;
static unsigned long append_segment_id;
static uint32_t append_segment_size;
static uint32_t recovered_generation;

static uint32_t count_segment_messages(const char *path)
{
    FILE *file;
    char line[OUTBOX_MESSAGE_MAX_LEN];
    uint32_t count = 0;

    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        ++count;
    }
    fclose(file);
    return count;
}

static bool oldest_segment(char path[64])
{
    DIR *dir;
    struct dirent *entry;
    unsigned long selected = 0;
    unsigned long value;

    dir = opendir(OUTBOX_DIR);
    if (dir == NULL) {
        return false;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (sscanf(entry->d_name, "OB%5lu.LOG", &value) == 1 &&
            (selected == 0 || value < selected)) {
            selected = value;
        }
    }
    closedir(dir);
    if (selected == 0) {
        return false;
    }
    snprintf(path, 64, OUTBOX_DIR "/OB%05lu.LOG", selected);
    return true;
}

static unsigned long path_segment_id(const char *path)
{
    unsigned long value = 0;
    const char *name = strrchr(path, '/');

    if (name != NULL) {
        (void)sscanf(name + 1, "OB%5lu.LOG", &value);
    }
    return value;
}

static void storage_failure_locked(const char *message, int error_code)
{
    gateway_outbox_core_record_failure(&core);
    current_inflight = false;
    current_health = false;
    if (error_code == ENOSPC) {
        storage_manager_report_full(error_code);
    } else {
        ready = false;
        storage_manager_report_io_failure(error_code);
    }
    device_manager_request_ui_status_refresh();
    ESP_LOGE(TAG, "%s", message);
}

static bool write_durable_file(const char *path, const char *json)
{
    FILE *file = fopen(path, "w");
    bool ok;

    if (file == NULL) {
        return false;
    }
    ok = fputs(json, file) >= 0 && fputc('\n', file) != EOF && fflush(file) == 0 &&
         fsync(fileno(file)) == 0;
    fclose(file);
    return ok;
}

static bool promote_next_health_locked(void)
{
    if (access(OUTBOX_HEALTH_FILE, F_OK) != 0 && access(OUTBOX_HEALTH_NEXT_FILE, F_OK) == 0 &&
        rename(OUTBOX_HEALTH_NEXT_FILE, OUTBOX_HEALTH_FILE) != 0) {
        storage_failure_locked("unable to promote pending health record", errno);
        return false;
    }
    return true;
}

static void recover_locked(void)
{
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char path[96];
    unsigned long value;
    uint32_t failures = core.failure_count;

    ready = false;
    read_path[0] = '\0';
    read_offset = 0;
    current_next_offset = 0;
    current_inflight = false;
    current_health = false;
    gateway_outbox_core_init(&core, OUTBOX_MAX_BYTES);
    core.failure_count = failures;
    append_segment_id = 0;
    append_segment_size = 0;

    if (mkdir(OUTBOX_DIR, 0775) != 0 && errno != EEXIST) {
        storage_failure_locked("cannot create outbox directory", errno);
        return;
    }
    dir = opendir(OUTBOX_DIR);
    if (dir == NULL) {
        storage_failure_locked("cannot open outbox directory", errno);
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        snprintf(path, sizeof(path), OUTBOX_DIR "/%s", entry->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        if (sscanf(entry->d_name, "OB%5lu.LOG", &value) == 1) {
            gateway_outbox_core_recover_segment(&core, (uint32_t)st.st_size,
                                                count_segment_messages(path));
            if (value >= append_segment_id) {
                append_segment_id = value;
                append_segment_size = (uint32_t)st.st_size;
            }
        }
    }
    closedir(dir);
    if (!promote_next_health_locked()) {
        return;
    }
    ready = true;
    recovered_generation = storage_manager_generation();
    ESP_LOGI(TAG, "outbox recovered; %lu bytes, %lu messages", (unsigned long)core.stored_bytes,
             (unsigned long)core.pending_messages);
    device_manager_request_ui_status_refresh();
}

static void sync_storage_locked(void)
{
    if (storage_manager_generation() != recovered_generation) {
        recover_locked();
    }
}

static bool append_segment_locked(const char *json)
{
    char path[64];
    FILE *file;
    size_t bytes = strlen(json) + 1U;
    bool ok;

    if (!gateway_outbox_core_can_append(&core, (uint32_t)bytes)) {
        gateway_outbox_core_record_failure(&core);
        ESP_LOGE(TAG, "outbox full; broadcast event cannot be persisted");
        return false;
    }
    if (append_segment_id == 0) {
        append_segment_id = 1;
    }
    if (append_segment_size + bytes > OUTBOX_SEGMENT_BYTES ||
        (read_path[0] != '\0' && append_segment_id == path_segment_id(read_path))) {
        ++append_segment_id;
        append_segment_size = 0;
    }
    snprintf(path, sizeof(path), OUTBOX_DIR "/OB%05lu.LOG", append_segment_id);
    file = fopen(path, "a");
    if (file == NULL) {
        storage_failure_locked("unable to open broadcast segment", errno);
        return false;
    }
    ok = fputs(json, file) >= 0 && fputc('\n', file) != EOF && fflush(file) == 0 &&
         fsync(fileno(file)) == 0;
    fclose(file);
    if (!ok) {
        storage_failure_locked("broadcast persistence failed", errno);
        return false;
    }
    gateway_outbox_core_record_append(&core, (uint32_t)bytes);
    append_segment_size += (uint32_t)bytes;
    device_manager_request_ui_status_refresh();
    return true;
}

void gateway_outbox_init(void)
{
    gateway_outbox_sync_storage();
}

void gateway_outbox_sync_storage(void)
{
    if (!storage_manager_lock()) {
        ready = false;
        return;
    }
    sync_storage_locked();
    storage_manager_unlock();
}

bool gateway_outbox_is_ready(void)
{
    return ready && storage_manager_is_ready();
}

bool gateway_outbox_enqueue_broadcast(const char *json)
{
    bool result;

    if (json == NULL || !storage_manager_lock()) {
        return false;
    }
    sync_storage_locked();
    result = ready && append_segment_locked(json);
    storage_manager_unlock();
    return result;
}

bool gateway_outbox_store_health(const char *json)
{
    const char *path;
    bool result;

    if (json == NULL || storage_manager_is_full() || !storage_manager_lock()) {
        return false;
    }
    sync_storage_locked();
    if (!ready) {
        storage_manager_unlock();
        return false;
    }
    path = current_inflight && current_health ? OUTBOX_HEALTH_NEXT_FILE : OUTBOX_HEALTH_FILE;
    result = write_durable_file(path, json);
    if (!result) {
        storage_failure_locked("health persistence failed", errno);
    }
    storage_manager_unlock();
    return result;
}

bool gateway_outbox_next(char output[OUTBOX_MESSAGE_MAX_LEN], bool *is_health)
{
    char path[64];
    FILE *file;
    struct stat st;

    if (output == NULL || is_health == NULL || !storage_manager_lock()) {
        return false;
    }
    sync_storage_locked();
    if (!ready || current_inflight) {
        storage_manager_unlock();
        return false;
    }
    while (true) {
        if (read_path[0] == '\0') {
            if (!oldest_segment(path)) {
                break;
            }
            snprintf(read_path, sizeof(read_path), "%s", path);
            read_offset = 0;
        }
        file = fopen(read_path, "r");
        if (file == NULL || fseek(file, read_offset, SEEK_SET) != 0) {
            if (file != NULL) {
                fclose(file);
            }
            storage_failure_locked("unable to read outbox segment", errno);
            storage_manager_unlock();
            return false;
        }
        if (fgets(output, OUTBOX_MESSAGE_MAX_LEN, file) != NULL) {
            current_next_offset = ftell(file);
            fclose(file);
            output[strcspn(output, "\r\n")] = '\0';
            current_inflight = true;
            current_health = false;
            *is_health = false;
            storage_manager_unlock();
            return true;
        }
        fclose(file);
        if (stat(read_path, &st) != 0 || unlink(read_path) != 0) {
            storage_failure_locked("unable to remove acknowledged outbox segment", errno);
            storage_manager_unlock();
            return false;
        }
        gateway_outbox_core_remove_segment(&core, (uint32_t)st.st_size);
        storage_manager_report_write_success();
        if (append_segment_id == path_segment_id(read_path)) {
            append_segment_size = 0;
        }
        read_path[0] = '\0';
        read_offset = 0;
        device_manager_request_ui_status_refresh();
    }
    if (!promote_next_health_locked()) {
        storage_manager_unlock();
        return false;
    }
    file = fopen(OUTBOX_HEALTH_FILE, "r");
    if (file != NULL && fgets(output, OUTBOX_MESSAGE_MAX_LEN, file) != NULL) {
        fclose(file);
        output[strcspn(output, "\r\n")] = '\0';
        current_inflight = true;
        current_health = true;
        *is_health = true;
        storage_manager_unlock();
        return true;
    }
    if (file != NULL) {
        fclose(file);
    }
    storage_manager_unlock();
    return false;
}

void gateway_outbox_ack_current(void)
{
    if (!current_inflight) {
        return;
    }
    if (!storage_manager_lock()) {
        current_inflight = false;
        current_health = false;
        return;
    }
    sync_storage_locked();
    if (!ready) {
        storage_manager_unlock();
        current_inflight = false;
        current_health = false;
        return;
    }
    if (current_health) {
        if (unlink(OUTBOX_HEALTH_FILE) != 0 && errno != ENOENT) {
            storage_failure_locked("unable to acknowledge health record", errno);
        } else {
            (void)promote_next_health_locked();
            storage_manager_report_write_success();
        }
    } else {
        read_offset = current_next_offset;
        gateway_outbox_core_ack_broadcast(&core);
    }
    current_inflight = false;
    current_health = false;
    storage_manager_unlock();
    device_manager_request_ui_status_refresh();
}

void gateway_outbox_release_current(void)
{
    current_inflight = false;
    current_health = false;
}

uint32_t gateway_outbox_pending_count(void)
{
    return core.pending_messages;
}

uint32_t gateway_outbox_pending_bytes(void)
{
    return core.stored_bytes;
}

uint32_t gateway_outbox_failure_count(void)
{
    return core.failure_count;
}

const char *gateway_outbox_status_text(void)
{
    if (storage_manager_is_full()) {
        return "Full";
    }
    if (!gateway_outbox_is_ready()) {
        return "No SD";
    }
    if (core.stored_bytes >= core.capacity_bytes) {
        return "Full";
    }
    /* failure_count is an evidence counter for this boot, not a current
     * availability flag. A recovered Outbox must report Ready while retaining
     * the count for field diagnostics. */
    return "Ready";
}
