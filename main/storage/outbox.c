#include "outbox.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bsp/m5stack_core_s3.h"
#include "esp_log.h"

#include "csv_logger.h"

#define OUTBOX_DIR BSP_SD_MOUNT_POINT "/outbox"
#define OUTBOX_SEGMENT_BYTES (256U * 1024U)
#define OUTBOX_MAX_BYTES (256U * 1024U * 1024U)
static const char *TAG = "outbox";
static bool ready;
static FILE *read_file;
static char read_path[64];
static bool current_inflight;
static bool current_health;
static uint32_t stored_bytes;
static uint32_t pending_messages;
static unsigned long append_segment_id;
static uint32_t append_segment_size;

static uint32_t count_segment_messages(const char *path)
{
    FILE *file;
    char line[OUTBOX_MESSAGE_MAX_LEN];
    uint32_t count = 0;

    file = fopen(path, "r");
    if (file == NULL) return 0;
    while (fgets(line, sizeof(line), file) != NULL) ++count;
    fclose(file);
    return count;
}
static bool oldest_segment(char path[64])
{
    DIR *dir; struct dirent *entry; unsigned long selected=0; unsigned long value;
    dir=opendir(OUTBOX_DIR); if (!dir) return false;
    while ((entry=readdir(dir)) != NULL) {
        if (sscanf(entry->d_name, "OB%5lu.LOG", &value) == 1 && (selected == 0 || value < selected)) selected=value;
    }
    closedir(dir);
    if (selected == 0) return false;
    snprintf(path, 64, OUTBOX_DIR "/OB%05lu.LOG", selected);
    return true;
}
static bool append_segment(const char *json)
{
    char path[64]; FILE *file; size_t bytes;

    bytes = strlen(json) + 1U;
    if (stored_bytes + bytes > OUTBOX_MAX_BYTES) { ESP_LOGE(TAG,"outbox full; broadcast event cannot be persisted"); return false; }
    if (append_segment_id == 0) append_segment_id = 1;
    if (append_segment_size + bytes > OUTBOX_SEGMENT_BYTES) {
        ++append_segment_id;
        append_segment_size = 0;
    }
    snprintf(path,sizeof(path),OUTBOX_DIR "/OB%05lu.LOG",append_segment_id);
    file=fopen(path,"a"); if (!file) return false;
    bool ok=fputs(json,file)>=0 && fputc('\n',file)!=EOF && fflush(file)==0 && fsync(fileno(file))==0;
    fclose(file);
    if (ok) {
        stored_bytes += (uint32_t)bytes;
        append_segment_size += (uint32_t)bytes;
        ++pending_messages;
    }
    return ok;
}
void gateway_outbox_init(void)
{
    DIR *dir; struct dirent *entry; struct stat st; char path[96]; unsigned long value;
    ready=csv_logger_is_ready();
    if (!ready) { ESP_LOGW(TAG,"SD unavailable; real-time MQTT only"); return; }
    if (mkdir(OUTBOX_DIR,0775) && errno != EEXIST) { ready=false; ESP_LOGE(TAG,"cannot create outbox directory"); return; }
    stored_bytes = 0; pending_messages = 0; append_segment_id = 0; append_segment_size = 0;
    dir = opendir(OUTBOX_DIR);
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            snprintf(path, sizeof(path), OUTBOX_DIR "/%s", entry->d_name);
            if (stat(path, &st) || !S_ISREG(st.st_mode)) continue;
            stored_bytes += (uint32_t)st.st_size;
            if (sscanf(entry->d_name, "OB%5lu.LOG", &value) == 1) {
                pending_messages += count_segment_messages(path);
                if (value >= append_segment_id) {
                    append_segment_id = value;
                    append_segment_size = (uint32_t)st.st_size;
                }
            }
        }
        closedir(dir);
    }
    ESP_LOGI(TAG,"outbox recovered; %lu bytes, %lu messages",(unsigned long)stored_bytes,(unsigned long)pending_messages);
}
bool gateway_outbox_is_ready(void) { return ready; }
bool gateway_outbox_enqueue_broadcast(const char *json) { return ready && json && append_segment(json); }
bool gateway_outbox_store_health(const char *json)
{
    FILE *file; char path[64]; if (!ready || !json) return false;
    snprintf(path,sizeof(path),OUTBOX_DIR "/HEALTH.LOG"); file=fopen(path,"w"); if (!file) return false;
    bool ok=fputs(json,file)>=0 && fflush(file)==0 && fsync(fileno(file))==0; fclose(file); return ok;
}
bool gateway_outbox_next(char output[OUTBOX_MESSAGE_MAX_LEN], bool *is_health)
{
    char path[64]; FILE *health;
    if (!ready || current_inflight) return false;
    if (read_file == NULL && oldest_segment(path)) { snprintf(read_path,sizeof(read_path),"%s",path); read_file=fopen(read_path,"r"); }
    if (read_file != NULL) {
        if (fgets(output,OUTBOX_MESSAGE_MAX_LEN,read_file) != NULL) { output[strcspn(output,"\r\n")]='\0'; current_inflight=true; current_health=false; *is_health=false; return true; }
        struct stat st;
        fclose(read_file); read_file=NULL;
        if (!stat(read_path, &st) && stored_bytes >= (uint32_t)st.st_size) stored_bytes -= (uint32_t)st.st_size;
        if (strstr(read_path, "OB") != NULL && append_segment_id != 0) {
            unsigned long value;
            if (sscanf(strrchr(read_path, '/') + 1, "OB%5lu.LOG", &value) == 1 && value == append_segment_id) append_segment_size = 0;
        }
        unlink(read_path); read_path[0]='\0'; return gateway_outbox_next(output,is_health);
    }
    health=fopen(OUTBOX_DIR "/HEALTH.LOG","r"); if (health && fgets(output,OUTBOX_MESSAGE_MAX_LEN,health)) { fclose(health); output[strcspn(output,"\r\n")]='\0'; current_inflight=true; current_health=true; *is_health=true; return true; }
    if (health) fclose(health);
    return false;
}
void gateway_outbox_ack_current(void) { if (current_health) unlink(OUTBOX_DIR "/HEALTH.LOG"); else if (pending_messages > 0) --pending_messages; current_health=false; current_inflight=false; }
void gateway_outbox_release_current(void) { if (read_file) { fclose(read_file); read_file=fopen(read_path,"r"); } current_inflight=false; }
uint32_t gateway_outbox_pending_count(void)
{
    return ready ? pending_messages : 0;
}
const char *gateway_outbox_status_text(void) { return ready ? "Ready" : "No SD"; }
