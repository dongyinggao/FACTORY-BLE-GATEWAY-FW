#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void (*mqtt_service_ota_command_handler_t)(const char *payload);

void mqtt_service_start(void);
bool mqtt_service_is_connected(void);
int mqtt_service_publish(const char *payload);
int mqtt_service_publish_to_topic(const char *topic, const char *payload);
bool mqtt_service_take_puback(int *message_id);
const char *mqtt_service_status_text(void);
bool mqtt_service_is_secure_transport(void);
void mqtt_service_set_ota_command_handler(mqtt_service_ota_command_handler_t handler);
uint32_t mqtt_service_ota_command_dropped(void);
