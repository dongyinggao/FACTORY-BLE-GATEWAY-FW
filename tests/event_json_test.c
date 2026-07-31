#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "event_json.h"

int main(void)
{
    gateway_config_t config = {.gateway_id="GW-01", .gateway_location="Room101"};
    gateway_broadcast_message_t message = {0};
    char json[GATEWAY_JSON_MAX_LEN];
    gateway_event_id_make(message.event_id, sizeof(message.event_id), 0x12AB, 7);
    assert(strcmp(message.event_id, "000012AB-7") == 0);
    strcpy(message.device.name, "SM_iCM2");
    strcpy(message.device.broadcast_id, "BCAST-1");
    message.device.address[5] = 0xAA;
    message.type = GATEWAY_BROADCAST_STARTED;
    message.event_uptime_s = 17;
    message.broadcast_duration_s = 19;
    assert(gateway_json_encode_broadcast(json, sizeof(json), &message, &config) > 0);
    assert(strstr(json, "\"message_type\":\"broadcast\"") != NULL);
    assert(strstr(json, "\"event\":\"BROADCAST_STARTED\"") != NULL);
    assert(strstr(json, "\"event_id\":\"000012AB-7\"") != NULL);
    assert(strstr(json, "\"broadcast_id\":\"BCAST-1\"") != NULL);
    assert(strstr(json, "\"device_mac\":\"AA:00:00:00:00:00\"") != NULL);
    assert(strstr(json, "\"broadcast_duration_s\":null") != NULL);
    message.type = GATEWAY_BROADCAST_ENDED;
    assert(gateway_json_encode_broadcast(json, sizeof(json), &message, &config) > 0);
    assert(strstr(json, "\"broadcast_duration_s\":19") != NULL);
    gateway_health_message_t health = {
        .event_id = "000012AB-8",
        .config = &config,
        .uptime_s = 30,
        .wifi = "Connected",
        .mqtt = "Connected",
        .sntp = "Synced",
        .sd_ready = true,
        .sd_status = "OK",
        .outbox_messages = 2,
        .outbox_bytes = 512,
        .outbox_failures = 3,
        .registered_devices = 4,
        .broadcasting_devices = 2,
        .scan_reports_30s = 99,
        .filter_matched_30s = 12,
        .scan_queue_high_water = 2,
        .ui_queue_high_water = 3,
        .capture_queue_high_water = 4,
        .upload_queue_high_water = 5,
        .scan_dropped = 1,
        .ui_dropped = 2,
        .capture_dropped = 0,
        .upload_dropped = 1,
    };
    assert(gateway_json_encode_health(json, sizeof(json), &health) > 0);
    assert(strstr(json, "\"outbox_bytes\":512") != NULL);
    assert(strstr(json, "\"outbox_failures\":3") != NULL);
    assert(strstr(json, "\"registered_devices\":4") != NULL);
    assert(strstr(json, "\"filter_matched_30s\":12") != NULL);
    assert(strstr(json, "\"queue_high_water\":{\"scan\":2,\"ui\":3,\"capture\":4,\"upload\":5}") != NULL);
    assert(strstr(json, "\"dropped_events\":{\"scan\":1,\"ui\":2,\"capture\":0,\"upload\":1}") != NULL);
    puts("event_json tests passed");
    return 0;
}
