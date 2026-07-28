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
    strcpy(message.device.report.name, "SM_iCM2");
    message.device.report.address[5] = 0xAA;
    message.type = GATEWAY_BROADCAST_STARTED;
    message.event_uptime_s = 17;
    assert(gateway_json_encode_broadcast(json, sizeof(json), &message, &config) > 0);
    assert(strstr(json, "\"message_type\":\"broadcast\"") != NULL);
    assert(strstr(json, "\"event\":\"BROADCAST_STARTED\"") != NULL);
    assert(strstr(json, "\"event_id\":\"000012AB-7\"") != NULL);
    puts("event_json tests passed");
    return 0;
}
