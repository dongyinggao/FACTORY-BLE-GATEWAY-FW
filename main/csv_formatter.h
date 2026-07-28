#pragma once

#include <stddef.h>

#include "device_manager_core.h"
#include "gateway_config.h"

#define CSV_LIFECYCLE_LINE_MAX_LEN 384

typedef enum {
    CSV_LIFECYCLE_BROADCAST_STARTED,
    CSV_LIFECYCLE_BROADCAST_ENDED,
} csv_lifecycle_event_type_t;

typedef struct {
    csv_lifecycle_event_type_t type;
    managed_device_t device;
} csv_lifecycle_event_t;

/* Formats one unsynchronised lifecycle record, including its terminating newline. */
int csv_format_lifecycle_event(char *output, size_t output_size,
                               const csv_lifecycle_event_t *event,
                               const gateway_config_t *config);
