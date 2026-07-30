#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t capacity_bytes;
    uint32_t stored_bytes;
    uint32_t pending_messages;
    uint32_t failure_count;
} gateway_outbox_core_t;

void gateway_outbox_core_init(gateway_outbox_core_t *core, uint32_t capacity_bytes);
bool gateway_outbox_core_can_append(const gateway_outbox_core_t *core, uint32_t bytes);
void gateway_outbox_core_record_append(gateway_outbox_core_t *core, uint32_t bytes);
void gateway_outbox_core_recover_segment(gateway_outbox_core_t *core, uint32_t bytes,
                                         uint32_t messages);
void gateway_outbox_core_ack_broadcast(gateway_outbox_core_t *core);
void gateway_outbox_core_remove_segment(gateway_outbox_core_t *core, uint32_t bytes);
void gateway_outbox_core_record_failure(gateway_outbox_core_t *core);
