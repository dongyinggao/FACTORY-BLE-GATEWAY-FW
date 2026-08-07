#include "event_json.h"

#include <stdarg.h>
#include <stdio.h>

static bool append(char *out, size_t size, size_t *used, const char *format, ...)
{
    va_list args; int written;
    va_start(args, format); written = vsnprintf(out + *used, size - *used, format, args); va_end(args);
    if (written < 0 || (size_t)written >= size - *used) return false;
    *used += (size_t)written; return true;
}
static bool quoted(char *out, size_t size, size_t *used, const char *text)
{
    if (!append(out, size, used, "\"")) return false;
    for (; text != NULL && *text; ++text) {
        if (*text == '"' || *text == '\\') if (!append(out, size, used, "\\")) return false;
        if (!append(out, size, used, "%c", *text)) return false;
    }
    return append(out, size, used, "\"");
}
static bool key_string(char *out, size_t size, size_t *used, const char *key, const char *value, bool comma)
{ return append(out,size,used,"\"%s\":",key) && quoted(out,size,used,value) && (!comma || append(out,size,used,",")); }
void gateway_event_id_make(char *output, size_t output_size, uint32_t boot_id, uint32_t sequence)
{ snprintf(output, output_size, "%08lX-%lu", (unsigned long)boot_id, (unsigned long)sequence); }
int gateway_json_encode_broadcast(char *out, size_t size, const gateway_broadcast_message_t *m, const gateway_config_t *c)
{
    char mac[18]; size_t used=0; const char *event;
    if (!out || !m || !c || !size) return -1;
    switch (m->type) {
    case GATEWAY_BROADCAST_STARTED: event = "BROADCAST_STARTED"; break;
    case GATEWAY_BROADCAST_ACTIVE: event = "BROADCAST_ACTIVE"; break;
    case GATEWAY_BROADCAST_ENDED: event = "BROADCAST_ENDED"; break;
    default: return -1;
    }
    snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",m->device.address[5],m->device.address[4],m->device.address[3],m->device.address[2],m->device.address[1],m->device.address[0]);
    if (!append(out,size,&used,"{") || !key_string(out,size,&used,"message_type","broadcast",true) ||
        !key_string(out,size,&used,"event_id",m->event_id,true) ||
        !key_string(out,size,&used,"broadcast_id",m->device.broadcast_id,true) ||
        !key_string(out,size,&used,"gateway_id",c->gateway_id,true) ||
        !key_string(out,size,&used,"gateway_location",c->gateway_location,true) ||
        !key_string(out,size,&used,"event",event,true) ||
        !key_string(out,size,&used,"device_mac",mac,true) ||
        !key_string(out,size,&used,"device_name",m->device.name,true) ||
        !append(out,size,&used,"\"observed_rssi\":%d,\"time_synced\":%s,\"event_uptime_s\":%lu,",
                m->device.rssi, m->time_synced?"true":"false", (unsigned long)m->event_uptime_s) ||
        !key_string(out,size,&used,"recorded_at",m->recorded_at,true) ||
        !key_string(out,size,&used,"broadcast_started_at",m->broadcast_started_at,true) ||
        !key_string(out,size,&used,"broadcast_ended_at",m->broadcast_ended_at,true) ||
        (m->type != GATEWAY_BROADCAST_STARTED &&
         !append(out,size,&used,"\"broadcast_duration_s\":%lu,",(unsigned long)m->broadcast_duration_s)) ||
        (m->type == GATEWAY_BROADCAST_STARTED && !append(out,size,&used,"\"broadcast_duration_s\":null,")) ||
        !key_string(out,size,&used,"end_detected_at",m->end_detected_at,false) || !append(out,size,&used,"}")) return -1;
    return (int)used;
}
int gateway_json_encode_health(char *out, size_t size, const gateway_health_message_t *m)
{
    size_t used=0;
    if (!out || !m || !m->event_id || !m->config) return -1;
    if (!append(out,size,&used,"{") ||
        !key_string(out,size,&used,"message_type","gateway_health",true) ||
        !key_string(out,size,&used,"event_id",m->event_id,true) ||
        !key_string(out,size,&used,"gateway_id",m->config->gateway_id,true) ||
        !append(out,size,&used,"\"uptime_s\":%lu,",(unsigned long)m->uptime_s) ||
        !key_string(out,size,&used,"wifi",m->wifi,true) ||
        !key_string(out,size,&used,"mqtt",m->mqtt,true) ||
        !key_string(out,size,&used,"sntp",m->sntp,true) ||
        !append(out,size,&used,"\"sd_ready\":%s,",m->sd_ready?"true":"false") ||
        !key_string(out,size,&used,"sd_status",m->sd_status,true) ||
        !append(out,size,&used,"\"sd_error\":%ld,\"outbox_messages\":%lu,\"outbox_bytes\":%lu,\"outbox_failures\":%lu,",
                (long)m->sd_error, (unsigned long)m->outbox_messages,
                (unsigned long)m->outbox_bytes, (unsigned long)m->outbox_failures) ||
        !append(out,size,&used,"\"registered_devices\":%u,\"broadcasting_devices\":%u,\"scan_reports_30s\":%lu,\"filter_matched_30s\":%lu,",
                (unsigned int)m->registered_devices, (unsigned int)m->broadcasting_devices,
                (unsigned long)m->scan_reports_30s, (unsigned long)m->filter_matched_30s) ||
        !append(out,size,&used,"\"scan_timing_30s\":{\"callback_avg_us\":%lu,\"callback_max_us\":%lu,\"queue_wait_samples\":%lu,\"queue_wait_avg_us\":%lu,\"queue_wait_max_us\":%lu},",
                (unsigned long)m->scan_callback_avg_us, (unsigned long)m->scan_callback_max_us,
                (unsigned long)m->scan_queue_wait_samples, (unsigned long)m->scan_queue_wait_avg_us,
                (unsigned long)m->scan_queue_wait_max_us) ||
        !append(out,size,&used,"\"delivery\":{\"volatile_published\":%lu,\"unrecoverable_dropped\":%lu},"
                             "\"dropped_events\":{\"scan\":%lu,\"ui\":%lu,\"capture\":%lu,\"upload\":%lu}}",
                (unsigned long)m->volatile_published,
                (unsigned long)m->unrecoverable_upload_dropped,
                (unsigned long)m->scan_dropped, (unsigned long)m->ui_dropped,
                (unsigned long)m->capture_dropped, (unsigned long)m->upload_dropped)) return -1;
    return (int)used;
}
