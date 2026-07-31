#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool ready;
    bool mounted;
    bool full;
    uint32_t generation;
    uint32_t retry_at_s;
} storage_state_core_t;

void storage_state_core_init(storage_state_core_t *state);
void storage_state_core_mount_result(storage_state_core_t *state, bool success, uint32_t now_s,
                                     uint32_t retry_delay_s);
void storage_state_core_mark_failed(storage_state_core_t *state, uint32_t now_s,
                                    uint32_t retry_delay_s);
void storage_state_core_mark_full(storage_state_core_t *state);
void storage_state_core_mark_write_success(storage_state_core_t *state);
bool storage_state_core_retry_due(const storage_state_core_t *state, uint32_t now_s);
