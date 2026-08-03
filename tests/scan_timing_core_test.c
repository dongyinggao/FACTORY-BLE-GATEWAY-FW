#include <assert.h>
#include <stdio.h>

#include "scan_timing_core.h"

int main(void)
{
    ble_scan_timing_core_t core;
    ble_scan_timing_window_t window;

    ble_scan_timing_core_init(&core);
    ble_scan_timing_core_record_callback(&core, 10);
    ble_scan_timing_core_record_callback(&core, 20);
    ble_scan_timing_core_record_callback(&core, 31);
    ble_scan_timing_core_record_queue_wait(&core, 100);
    ble_scan_timing_core_record_queue_wait(&core, 300);
    ble_scan_timing_core_take_window(&core, &window);
    assert(window.callback_samples == 3);
    assert(window.callback_avg_us == 20);
    assert(window.callback_max_us == 31);
    assert(window.queue_wait_samples == 2);
    assert(window.queue_wait_avg_us == 200);
    assert(window.queue_wait_max_us == 300);

    ble_scan_timing_core_take_window(&core, &window);
    assert(window.callback_samples == 0);
    assert(window.callback_avg_us == 0);
    assert(window.queue_wait_samples == 0);
    puts("scan timing core tests passed");
    return 0;
}
