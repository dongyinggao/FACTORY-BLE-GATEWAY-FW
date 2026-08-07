#include <assert.h>
#include <stdio.h>

#include "time_service_core.h"

int main(void)
{
    assert(time_service_event_is_after_sync(100, 100));
    assert(time_service_event_is_after_sync(101, 100));
    assert(!time_service_event_is_after_sync(99, 100));

    /* Verify a timestamp beyond the 32-bit millisecond range remains exact. */
    assert(time_service_elapsed_ms(4320000025ULL, 4320000000ULL) == 25ULL);
    assert(time_service_elapsed_ms(8640000000ULL, 4320000000ULL) == 4320000000ULL);

    puts("time_service core tests passed");
    return 0;
}
