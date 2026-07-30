#pragma once

#include <stddef.h>

#include "device_lifecycle.h"
#include "gateway_config.h"

#define CSV_LIFECYCLE_LINE_MAX_LEN 384

typedef device_lifecycle_event_t csv_lifecycle_event_t;

/* Formats one unsynchronised lifecycle record, including its terminating newline. */
int csv_format_lifecycle_event(char *output, size_t output_size,
                               const csv_lifecycle_event_t *event,
                               const gateway_config_t *config);
