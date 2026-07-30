#pragma once

#include <stdbool.h>

typedef struct {
    bool awaiting;
    int message_id;
} gateway_publisher_ack_t;

void gateway_publisher_ack_reset(gateway_publisher_ack_t *state);
bool gateway_publisher_ack_begin(gateway_publisher_ack_t *state, int message_id);
bool gateway_publisher_ack_accept(gateway_publisher_ack_t *state, int message_id);
