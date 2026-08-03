#pragma once

#include <stdint.h>

typedef struct {
    uint32_t callback_samples;
    uint32_t callback_avg_us;
    uint32_t callback_max_us;
    uint32_t queue_wait_samples;
    uint32_t queue_wait_avg_us;
    uint32_t queue_wait_max_us;
} ble_scan_timing_window_t;

typedef struct {
    uint64_t callback_total_us;
    uint64_t queue_wait_total_us;
    uint32_t callback_samples;
    uint32_t callback_max_us;
    uint32_t queue_wait_samples;
    uint32_t queue_wait_max_us;
} ble_scan_timing_core_t;

void ble_scan_timing_core_init(ble_scan_timing_core_t *core);
void ble_scan_timing_core_record_callback(ble_scan_timing_core_t *core, uint32_t duration_us);
void ble_scan_timing_core_record_queue_wait(ble_scan_timing_core_t *core, uint32_t duration_us);
void ble_scan_timing_core_take_window(ble_scan_timing_core_t *core,
                                      ble_scan_timing_window_t *window);
