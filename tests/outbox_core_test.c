#include <assert.h>
#include <stdio.h>

#include "outbox_core.h"

int main(void)
{
    gateway_outbox_core_t core;

    gateway_outbox_core_init(&core, 100U);
    assert(core.capacity_bytes == 100U);
    assert(gateway_outbox_core_can_append(&core, 60U));

    gateway_outbox_core_record_append(&core, 60U);
    assert(core.stored_bytes == 60U);
    assert(core.pending_messages == 1U);
    assert(!gateway_outbox_core_can_append(&core, 41U));
    assert(gateway_outbox_core_can_append(&core, 40U));

    gateway_outbox_core_record_failure(&core);
    assert(core.failure_count == 1U);

    gateway_outbox_core_recover_segment(&core, 20U, 2U);
    assert(core.stored_bytes == 80U);
    assert(core.pending_messages == 3U);

    gateway_outbox_core_ack_broadcast(&core);
    assert(core.pending_messages == 2U);
    gateway_outbox_core_remove_segment(&core, 60U);
    assert(core.stored_bytes == 20U);
    gateway_outbox_core_remove_segment(&core, 99U);
    assert(core.stored_bytes == 0U);

    puts("outbox core tests passed");
    return 0;
}
