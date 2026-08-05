#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void time_service_init(void);
void time_service_start_sync(void);
bool time_service_is_synced(void);
bool time_service_format_wall_ms(uint64_t wall_ms, char *output, size_t output_size);
const char *time_service_status_text(void);
