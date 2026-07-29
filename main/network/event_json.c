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
    event = m->type == GATEWAY_BROADCAST_STARTED ? "BROADCAST_STARTED" : "BROADCAST_ENDED";
    snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",m->device.address[5],m->device.address[4],m->device.address[3],m->device.address[2],m->device.address[1],m->device.address[0]);
    if (!append(out,size,&used,"{") || !key_string(out,size,&used,"message_type","broadcast",true) ||
        !key_string(out,size,&used,"event_id",m->event_id,true) || !key_string(out,size,&used,"gateway_id",c->gateway_id,true) ||
        !key_string(out,size,&used,"gateway_location",c->gateway_location,true) || !key_string(out,size,&used,"event",event,true) ||
        !key_string(out,size,&used,"device_id",mac,true) || !key_string(out,size,&used,"device_name",m->device.name,true) ||
        !key_string(out,size,&used,"mac",mac,true) || !append(out,size,&used,"\"address_type\":%u,",m->device.address_type) ||
        !append(out,size,&used,"\"rssi\":%d,\"time_synced\":%s,\"event_uptime_s\":%lu,",m->device.rssi,m->time_synced?"true":"false",(unsigned long)m->event_uptime_s) ||
        !key_string(out,size,&used,"timestamp",m->timestamp,true) || !key_string(out,size,&used,"broadcast_started_at",m->broadcast_started_at,true) ||
        !key_string(out,size,&used,"last_seen_at",m->last_seen_at,true) || !key_string(out,size,&used,"end_detected_at",m->end_detected_at,false) || !append(out,size,&used,"}")) return -1;
    return (int)used;
}
int gateway_json_encode_health(char *out, size_t size, const char *event_id, const gateway_config_t *c, uint32_t uptime_s, const char *wifi, const char *mqtt, const char *sntp, bool sd, uint32_t outbox, uint32_t capture_dropped, uint32_t upload_dropped)
{
    size_t used=0;
    if (!out || !event_id || !c) return -1;
    if (!append(out,size,&used,"{") || !key_string(out,size,&used,"message_type","gateway_health",true) || !key_string(out,size,&used,"event_id",event_id,true) || !key_string(out,size,&used,"gateway_id",c->gateway_id,true) || !append(out,size,&used,"\"uptime_s\":%lu,",(unsigned long)uptime_s) || !key_string(out,size,&used,"wifi",wifi,true) || !key_string(out,size,&used,"mqtt",mqtt,true) || !key_string(out,size,&used,"sntp",sntp,true) || !append(out,size,&used,"\"sd_ready\":%s,\"outbox_messages\":%lu,\"capture_dropped\":%lu,\"upload_dropped\":%lu}",sd?"true":"false",(unsigned long)outbox,(unsigned long)capture_dropped,(unsigned long)upload_dropped)) return -1;
    return (int)used;
}
