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
    message.device.address[5] = 0xAA;
    message.type = GATEWAY_BROADCAST_STARTED;
    message.event_uptime_s = 17;
    assert(gateway_json_encode_broadcast(json, sizeof(json), &message, &config) > 0);
    assert(strstr(json, "\"message_type\":\"broadcast\"") != NULL);
    assert(strstr(json, "\"event\":\"BROADCAST_STARTED\"") != NULL);
    assert(strstr(json, "\"event_id\":\"000012AB-7\"") != NULL);
    assert(gateway_json_encode_health(json, sizeof(json), "000012AB-8", &config, 30,
                                      "Connected", "Connected", "Synced", true, 2, 512, 3,
                                      0, 1) > 0);
    assert(strstr(json, "\"outbox_bytes\":512") != NULL);
    assert(strstr(json, "\"outbox_failures\":3") != NULL);
    assert(strstr(json, "\"capture_dropped\":0") != NULL);
    assert(strstr(json, "\"upload_dropped\":1") != NULL);
    puts("event_json tests passed");
    return 0;
}
