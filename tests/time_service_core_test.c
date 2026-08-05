#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "time_service_core.h"

int main(void)
{
    assert(time_service_event_is_after_sync(200ULL, 100ULL));
    assert(time_service_event_is_after_sync(100ULL, 100ULL));
    assert(!time_service_event_is_after_sync(99ULL, 100ULL));

    /* A 64-bit esp_timer millisecond base safely covers long-running gateways. */
    assert(time_service_event_is_after_sync(4320000025ULL, 4320000000ULL));
    assert(time_service_elapsed_ms(4320000025ULL, 4320000000ULL) == 25ULL);
    assert(time_service_elapsed_ms(250ULL, 200ULL) == 50ULL);

    puts("time_service core tests passed");
    return 0;
}
