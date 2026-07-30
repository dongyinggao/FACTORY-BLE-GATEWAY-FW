#include <assert.h>
#include <stdio.h>

#include "publisher_ack.h"

int main(void)
{
    gateway_publisher_ack_t state;

    gateway_publisher_ack_reset(&state);
    assert(!state.awaiting);
    assert(!gateway_publisher_ack_begin(&state, -1));
    assert(gateway_publisher_ack_begin(&state, 42));
    assert(state.awaiting);
    assert(!gateway_publisher_ack_begin(&state, 43));
    assert(!gateway_publisher_ack_accept(&state, 41));
    assert(state.awaiting);
    assert(gateway_publisher_ack_accept(&state, 42));
    assert(!state.awaiting);
    assert(!gateway_publisher_ack_accept(&state, 42));

    puts("publisher ACK tests passed");
    return 0;
}
