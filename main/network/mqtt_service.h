#pragma once

#include <stdbool.h>

void mqtt_service_start(void);
bool mqtt_service_is_connected(void);
int mqtt_service_publish(const char *payload);
bool mqtt_service_take_puback(int *message_id);
const char *mqtt_service_status_text(void);
