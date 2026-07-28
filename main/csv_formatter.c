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
    const managed_device_t *device;
    const char *event_name;
    uint32_t uptime_s;
    uint32_t end_detected_uptime_s;
    char event_time[32] = "";
    char started_time[32] = "";
    char last_seen_time[32] = "";
    char end_time[32] = "";
    bool synced;
    size_t used = 0;

    if (output == NULL || output_size == 0 || event == NULL || config == NULL ||
        (event->type != CSV_LIFECYCLE_BROADCAST_STARTED &&
         event->type != CSV_LIFECYCLE_BROADCAST_ENDED)) {
        return -1;
    }
    device = &event->device;
    event_name = event->type == CSV_LIFECYCLE_BROADCAST_STARTED ?
                     "BROADCAST_STARTED" : "BROADCAST_ENDED";
    uptime_s = (event->type == CSV_LIFECYCLE_BROADCAST_STARTED ?
                    device->broadcast_started_ms : device->end_detected_ms) / 1000U;
    end_detected_uptime_s = event->type == CSV_LIFECYCLE_BROADCAST_ENDED ?
                                 device->end_detected_ms / 1000U : 0;
    synced = time_service_format_wall_ms(event->type == CSV_LIFECYCLE_BROADCAST_STARTED ?
                                              device->broadcast_started_wall_ms : device->end_detected_wall_ms,
                                          event_time, sizeof(event_time));
    if (synced) {
        time_service_format_wall_ms(device->broadcast_started_wall_ms, started_time, sizeof(started_time));
        time_service_format_wall_ms(device->last_seen_wall_ms, last_seen_time, sizeof(last_seen_time));
        if (event->type == CSV_LIFECYCLE_BROADCAST_ENDED)
            time_service_format_wall_ms(device->end_detected_wall_ms, end_time, sizeof(end_time));
    }
    if (!append_quoted(output, output_size, &used, event_time) ||
        !append_format(output, output_size, &used, ",%s,%lu,%lu,%lu,", synced ? "true" : "false",
                       (unsigned long)uptime_s,
                       (unsigned long)(device->broadcast_started_ms / 1000U),
                       (unsigned long)(device->last_seen_ms / 1000U)) ||
        (end_detected_uptime_s != 0 &&
         !append_format(output, output_size, &used, "%lu,",
                        (unsigned long)end_detected_uptime_s)) ||
        (end_detected_uptime_s == 0 && !append_text(output, output_size, &used, ",")) ||
        !append_quoted(output, output_size, &used, config->gateway_id) ||
        !append_text(output, output_size, &used, ",") ||
        !append_quoted(output, output_size, &used, config->gateway_location) ||
        !append_format(output, output_size, &used, ",\"%02X:%02X:%02X:%02X:%02X:%02X\",",
                       device->report.address[5], device->report.address[4],
                       device->report.address[3], device->report.address[2],
                       device->report.address[1], device->report.address[0]) ||
        !append_quoted(output, output_size, &used, device->report.name) ||
        !append_format(output, output_size, &used,
                       ",\"%02X:%02X:%02X:%02X:%02X:%02X\",%u,%s,",
                       device->report.address[5], device->report.address[4],
                       device->report.address[3], device->report.address[2],
                       device->report.address[1], device->report.address[0],
                       device->report.address_type, event_name) ||
        !append_quoted(output, output_size, &used, started_time) || !append_text(output, output_size, &used, ",") ||
        !append_quoted(output, output_size, &used, last_seen_time) || !append_text(output, output_size, &used, ",") ||
        !append_quoted(output, output_size, &used, end_time) ||
        !append_format(output, output_size, &used, ",%d,SCANNING\n", device->report.rssi)) {
        return -1;
    }
    return (int)used;
}
