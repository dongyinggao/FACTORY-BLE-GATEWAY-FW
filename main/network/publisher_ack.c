#include "publisher_ack.h"

#include <stddef.h>

void gateway_publisher_ack_reset(gateway_publisher_ack_t *state)
{
    if (state != NULL) {
        *state = (gateway_publisher_ack_t){
            .message_id = -1,
        };
    }
}

bool gateway_publisher_ack_begin(gateway_publisher_ack_t *state, int message_id)
{
    if (state == NULL || state->awaiting || message_id < 0) {
        return false;
    }
    state->awaiting = true;
    state->message_id = message_id;
    return true;
}

bool gateway_publisher_ack_accept(gateway_publisher_ack_t *state, int message_id)
{
    if (state == NULL || !state->awaiting || state->message_id != message_id) {
        return false;
    }
    gateway_publisher_ack_reset(state);
    return true;
}
