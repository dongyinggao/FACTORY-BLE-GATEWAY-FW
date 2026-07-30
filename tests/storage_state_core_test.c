#include <assert.h>
#include <stdio.h>

#include "storage_state_core.h"

int main(void)
{
    storage_state_core_t state;

    storage_state_core_init(&state);
    assert(!state.ready);
    assert(storage_state_core_retry_due(&state, 0));

    storage_state_core_mount_result(&state, true, 0, 5);
    assert(state.ready && state.mounted && state.generation == 1);
    assert(!storage_state_core_retry_due(&state, 100));

    storage_state_core_mark_failed(&state, 10, 5);
    assert(!state.ready && state.mounted);
    assert(!storage_state_core_retry_due(&state, 14));
    assert(storage_state_core_retry_due(&state, 15));

    storage_state_core_mount_result(&state, false, 15, 5);
    assert(!state.ready && !state.mounted);
    assert(!storage_state_core_retry_due(&state, 19));
    assert(storage_state_core_retry_due(&state, 20));

    storage_state_core_mount_result(&state, true, 20, 5);
    assert(state.ready && state.generation == 2);
    puts("storage state tests passed");
    return 0;
}
