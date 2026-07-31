#include "scan_timing_core.h"

#include <stddef.h>

void ble_scan_timing_core_init(ble_scan_timing_core_t *core)
{
    if (core != NULL) {
        *core = (ble_scan_timing_core_t){0};
    }
}

void ble_scan_timing_core_record_callback(ble_scan_timing_core_t *core, uint32_t duration_us)
{
    if (core == NULL) {
        return;
    }
    ++core->callback_samples;
    core->callback_total_us += duration_us;
    if (duration_us > core->callback_max_us) {
        core->callback_max_us = duration_us;
    }
}

void ble_scan_timing_core_record_queue_wait(ble_scan_timing_core_t *core, uint32_t duration_us)
{
    if (core == NULL) {
        return;
    }
    ++core->queue_wait_samples;
    core->queue_wait_total_us += duration_us;
    if (duration_us > core->queue_wait_max_us) {
        core->queue_wait_max_us = duration_us;
    }
}

void ble_scan_timing_core_take_window(ble_scan_timing_core_t *core,
                                      ble_scan_timing_window_t *window)
{
    if (window != NULL) {
        *window = (ble_scan_timing_window_t){0};
    }
    if (core == NULL || window == NULL) {
        return;
    }
    window->callback_samples = core->callback_samples;
    window->callback_avg_us = core->callback_samples == 0U ? 0U :
                              (uint32_t)(core->callback_total_us / core->callback_samples);
    window->callback_max_us = core->callback_max_us;
    window->queue_wait_samples = core->queue_wait_samples;
    window->queue_wait_avg_us = core->queue_wait_samples == 0U ? 0U :
                                (uint32_t)(core->queue_wait_total_us / core->queue_wait_samples);
    window->queue_wait_max_us = core->queue_wait_max_us;
    ble_scan_timing_core_init(core);
}
