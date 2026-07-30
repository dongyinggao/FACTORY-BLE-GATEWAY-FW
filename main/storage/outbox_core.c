#include "outbox_core.h"

#include <stddef.h>

void gateway_outbox_core_init(gateway_outbox_core_t *core, uint32_t capacity_bytes)
{
    if (core == NULL) {
        return;
    }
    *core = (gateway_outbox_core_t){
        .capacity_bytes = capacity_bytes,
    };
}

bool gateway_outbox_core_can_append(const gateway_outbox_core_t *core, uint32_t bytes)
{
    return core != NULL && core->stored_bytes <= core->capacity_bytes &&
           bytes <= core->capacity_bytes - core->stored_bytes;
}

void gateway_outbox_core_record_append(gateway_outbox_core_t *core, uint32_t bytes)
{
    if (core == NULL) {
        return;
    }
    core->stored_bytes += bytes;
    ++core->pending_messages;
}

void gateway_outbox_core_recover_segment(gateway_outbox_core_t *core, uint32_t bytes,
                                         uint32_t messages)
{
    if (core == NULL) {
        return;
    }
    core->stored_bytes += bytes;
    core->pending_messages += messages;
}

void gateway_outbox_core_ack_broadcast(gateway_outbox_core_t *core)
{
    if (core != NULL && core->pending_messages > 0) {
        --core->pending_messages;
    }
}

void gateway_outbox_core_remove_segment(gateway_outbox_core_t *core, uint32_t bytes)
{
    if (core == NULL) {
        return;
    }
    core->stored_bytes = bytes >= core->stored_bytes ? 0 : core->stored_bytes - bytes;
}

void gateway_outbox_core_record_failure(gateway_outbox_core_t *core)
{
    if (core != NULL) {
        ++core->failure_count;
    }
}
