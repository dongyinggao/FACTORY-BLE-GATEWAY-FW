#pragma once

#include <stdbool.h>
#include <stdint.h>

bool time_service_event_is_after_sync(uint64_t event_ms, uint64_t sync_ms);
uint64_t time_service_elapsed_ms(uint64_t current_ms, uint64_t event_ms);
