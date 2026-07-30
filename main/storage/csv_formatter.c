#include "csv_formatter.h"

#include <stdarg.h>
#include <stdio.h>

#include "time_service.h"

static bool append_text(char *output, size_t output_size, size_t *used, const char *text)
{
    int written = snprintf(output + *used, output_size - *used, "%s", text);

    if (written < 0 || (size_t)written >= output_size - *used) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool append_format(char *output, size_t output_size, size_t *used, const char *format, ...)
{
    va_list arguments;
    int written;

    va_start(arguments, format);
    written = vsnprintf(output + *used, output_size - *used, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= output_size - *used) {
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool append_quoted(char *output, size_t output_size, size_t *used, const char *text)
{
    if (!append_text(output, output_size, used, "\"")) {
        return false;
    }
    for (; text != NULL && *text != '\0'; ++text) {
        if (*text == '"' && !append_text(output, output_size, used, "\"")) {
            return false;
        }
        if (!append_format(output, output_size, used, "%c", *text)) {
            return false;
        }
    }
    return append_text(output, output_size, used, "\"");
}

int csv_format_lifecycle_event(char *output, size_t output_size,
                               const csv_lifecycle_event_t *event,
                               const gateway_config_t *config)
{
    const char *event_name;
    uint32_t uptime_s;
    uint32_t broadcast_duration_s = 0;
    char event_time[32] = "";
    char started_time[32] = "";
    char last_seen_time[32] = "";
    char end_time[32] = "";
    bool synced;
    size_t used = 0;

    if (output == NULL || output_size == 0 || event == NULL || config == NULL ||
        (event->type != DEVICE_LIFECYCLE_BROADCAST_STARTED &&
         event->type != DEVICE_LIFECYCLE_BROADCAST_ENDED)) {
        return -1;
    }
    event_name = event->type == DEVICE_LIFECYCLE_BROADCAST_STARTED ?
                     "BROADCAST_STARTED" : "BROADCAST_ENDED";
    uptime_s = (event->type == DEVICE_LIFECYCLE_BROADCAST_STARTED ?
                    event->broadcast_started_ms : event->end_detected_ms) / 1000U;
    if (event->type == DEVICE_LIFECYCLE_BROADCAST_ENDED) {
        broadcast_duration_s = (event->last_seen_ms - event->broadcast_started_ms) / 1000U;
    }
    synced = time_service_format_wall_ms(event->type == DEVICE_LIFECYCLE_BROADCAST_STARTED ?
                                              event->broadcast_started_wall_ms : event->end_detected_wall_ms,
                                          event_time, sizeof(event_time));
    if (synced) {
        time_service_format_wall_ms(event->broadcast_started_wall_ms, started_time, sizeof(started_time));
        time_service_format_wall_ms(event->last_seen_wall_ms, last_seen_time, sizeof(last_seen_time));
        if (event->type == DEVICE_LIFECYCLE_BROADCAST_ENDED)
            time_service_format_wall_ms(event->end_detected_wall_ms, end_time, sizeof(end_time));
    }
    if (!append_quoted(output, output_size, &used, event_time) ||
        !append_format(output, output_size, &used, ",%s,%lu,", synced ? "true" : "false",
                       (unsigned long)uptime_s) ||
        !append_quoted(output, output_size, &used, config->gateway_id) ||
        !append_text(output, output_size, &used, ",") ||
        !append_quoted(output, output_size, &used, config->gateway_location) ||
        !append_format(output, output_size, &used, ",\"%02X:%02X:%02X:%02X:%02X:%02X\",",
                       event->address[5], event->address[4], event->address[3], event->address[2],
                       event->address[1], event->address[0]) ||
        !append_quoted(output, output_size, &used, event->name) ||
        !append_format(output, output_size, &used, ",%s,", event_name) ||
        !append_quoted(output, output_size, &used, event->broadcast_id) ||
        !append_text(output, output_size, &used, ",") ||
        !append_quoted(output, output_size, &used, started_time) || !append_text(output, output_size, &used, ",") ||
        !append_quoted(output, output_size, &used, last_seen_time) || !append_text(output, output_size, &used, ",") ||
        (event->type == DEVICE_LIFECYCLE_BROADCAST_ENDED &&
         !append_format(output, output_size, &used, "%lu,", (unsigned long)broadcast_duration_s)) ||
        (event->type == DEVICE_LIFECYCLE_BROADCAST_STARTED && !append_text(output, output_size, &used, ",")) ||
        !append_quoted(output, output_size, &used, end_time) ||
        !append_format(output, output_size, &used, ",%d\n", event->rssi)) {
        return -1;
    }
    return (int)used;
}
