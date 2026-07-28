#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "csv_formatter.h"

static csv_lifecycle_event_t event_for(csv_lifecycle_event_type_t type)
{
    csv_lifecycle_event_t event = {0};

    event.type = type;
    strcpy(event.device.report.name, "SM_ICM2");
    event.device.report.address[0] = 0xFF;
    event.device.report.address[1] = 0xEE;
    event.device.report.address[2] = 0xDD;
    event.device.report.address[3] = 0xCC;
    event.device.report.address[4] = 0xBB;
    event.device.report.address[5] = 0xAA;
    event.device.report.address_type = 1;
    event.device.report.rssi = -55;
    event.device.broadcast_started_ms = 22000;
    event.device.last_seen_ms = 45000;
    event.device.end_detected_ms = 105000;
    return event;
}

static void test_started_record_and_escaping(void)
{
    gateway_config_t config = {
        .gateway_id = "GW-01",
        .gateway_location = "Room,\"North\"",
    };
    csv_lifecycle_event_t event = event_for(CSV_LIFECYCLE_BROADCAST_STARTED);
    char line[CSV_LIFECYCLE_LINE_MAX_LEN];

    assert(csv_format_lifecycle_event(line, sizeof(line), &event, &config) > 0);
    assert(strstr(line, ",false,22,\"GW-01\",\"Room,\"\"North\"\"\",") != NULL);
    assert(strstr(line, "\"AA:BB:CC:DD:EE:FF\",\"SM_ICM2\"") != NULL);
    assert(strstr(line, ",BROADCAST_STARTED,,,,-55,SCANNING\n") != NULL);
}

static void test_ended_record_uses_end_detection_uptime(void)
{
    gateway_config_t config = {0};
    csv_lifecycle_event_t event = event_for(CSV_LIFECYCLE_BROADCAST_ENDED);
    char line[CSV_LIFECYCLE_LINE_MAX_LEN];

    assert(csv_format_lifecycle_event(line, sizeof(line), &event, &config) > 0);
    assert(strstr(line, ",false,105,") != NULL);
    assert(strstr(line, ",BROADCAST_ENDED,,,,-55,SCANNING\n") != NULL);
    assert(csv_format_lifecycle_event(line, 8, &event, &config) < 0);
}

int main(void)
{
    test_started_record_and_escaping();
    test_ended_record_uses_end_detection_uptime();
    puts("csv_formatter tests passed");
    return 0;
}
