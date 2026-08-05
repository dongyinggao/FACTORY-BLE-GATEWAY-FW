#include "time_service.h"

bool time_service_is_synced(void) { return false; }
bool time_service_format_wall_ms(uint64_t wall_ms, char *output, size_t output_size)
{ (void)wall_ms; if (output_size) output[0] = '\0'; return false; }
void time_service_init(void) {}
void time_service_start_sync(void) {}
const char *time_service_status_text(void) { return "Waiting"; }
