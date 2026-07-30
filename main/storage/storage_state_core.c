#include "storage_state_core.h"

#include <stddef.h>

void storage_state_core_init(storage_state_core_t *state)
{
    if (state != NULL) {
        *state = (storage_state_core_t){0};
    }
}

void storage_state_core_mount_result(storage_state_core_t *state, bool success, uint32_t now_s,
                                     uint32_t retry_delay_s)
{
    if (state == NULL) {
        return;
    }
    if (success) {
        state->ready = true;
        state->mounted = true;
        state->retry_at_s = 0;
        ++state->generation;
    } else {
        state->ready = false;
        state->mounted = false;
        state->retry_at_s = now_s + retry_delay_s;
    }
}

void storage_state_core_mark_failed(storage_state_core_t *state, uint32_t now_s,
                                    uint32_t retry_delay_s)
{
    if (state != NULL) {
        state->ready = false;
        state->retry_at_s = now_s + retry_delay_s;
    }
}

bool storage_state_core_retry_due(const storage_state_core_t *state, uint32_t now_s)
{
    return state != NULL && !state->ready && (int32_t)(now_s - state->retry_at_s) >= 0;
}
