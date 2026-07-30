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

static bool is_segment(const char *name)
{
    unsigned long value;
    return sscanf(name, "OB%5lu.LOG", &value) == 1;
}
static uint32_t directory_bytes(void)
{
    DIR *dir; struct dirent *entry; struct stat st; char path[96]; uint32_t total=0;
    dir=opendir(OUTBOX_DIR); if (!dir) return 0;
    while ((entry=readdir(dir)) != NULL) { snprintf(path,sizeof(path),OUTBOX_DIR "/%s",entry->d_name); if (!stat(path,&st) && S_ISREG(st.st_mode)) total += (uint32_t)st.st_size; }
    closedir(dir); return total;
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
    DIR *dir; struct dirent *entry; unsigned long highest=0, value; char path[64]; struct stat st; FILE *file;
    if (directory_bytes() + strlen(json) + 1U > OUTBOX_MAX_BYTES) { ESP_LOGE(TAG,"outbox full; broadcast event cannot be persisted"); return false; }
    dir=opendir(OUTBOX_DIR); if (!dir) return false;
    while ((entry=readdir(dir)) != NULL) if (sscanf(entry->d_name,"OB%5lu.LOG",&value)==1 && value>highest) highest=value;
    closedir(dir); if (!highest) highest=1;
    snprintf(path,sizeof(path),OUTBOX_DIR "/OB%05lu.LOG",highest);
    if (!stat(path,&st) && (uint32_t)st.st_size + strlen(json) + 1U > OUTBOX_SEGMENT_BYTES) { ++highest; snprintf(path,sizeof(path),OUTBOX_DIR "/OB%05lu.LOG",highest); }
    file=fopen(path,"a"); if (!file) return false; bool ok=fputs(json,file)>=0 && fputc('\n',file)!=EOF && fflush(file)==0 && fsync(fileno(file))==0; fclose(file); return ok;
}
void gateway_outbox_init(void)
{
    ready=csv_logger_is_ready();
    if (!ready) { ESP_LOGW(TAG,"SD unavailable; real-time MQTT only"); return; }
    if (mkdir(OUTBOX_DIR,0775) && errno != EEXIST) { ready=false; ESP_LOGE(TAG,"cannot create outbox directory"); return; }
    ESP_LOGI(TAG,"outbox recovered; %lu bytes",(unsigned long)directory_bytes());
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
        fclose(read_file); read_file=NULL; unlink(read_path); read_path[0]='\0'; return gateway_outbox_next(output,is_health);
    }
    health=fopen(OUTBOX_DIR "/HEALTH.LOG","r"); if (health && fgets(output,OUTBOX_MESSAGE_MAX_LEN,health)) { fclose(health); output[strcspn(output,"\r\n")]='\0'; current_inflight=true; current_health=true; *is_health=true; return true; }
    if (health) fclose(health);
    return false;
}
void gateway_outbox_ack_current(void) { if (current_health) unlink(OUTBOX_DIR "/HEALTH.LOG"); current_health=false; current_inflight=false; }
void gateway_outbox_release_current(void) { if (read_file) { fclose(read_file); read_file=fopen(read_path,"r"); } current_inflight=false; }
uint32_t gateway_outbox_pending_count(void)
{
    DIR *dir; struct dirent *entry; uint32_t count=0; char path[64]; FILE *file; char line[OUTBOX_MESSAGE_MAX_LEN];
    if (!ready || !(dir=opendir(OUTBOX_DIR))) return 0;
    while ((entry=readdir(dir)) != NULL) if (is_segment(entry->d_name)) { snprintf(path,sizeof(path),OUTBOX_DIR "/%s",entry->d_name); if ((file=fopen(path,"r"))) { while(fgets(line,sizeof(line),file)) ++count; fclose(file); } }
    closedir(dir); return count;
}
const char *gateway_outbox_status_text(void) { return ready ? "Ready" : "No SD"; }
