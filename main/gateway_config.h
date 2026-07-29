#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GATEWAY_ID_MAX_LEN 32
#define GATEWAY_LOCATION_MAX_LEN 48

typedef struct {
    char gateway_id[GATEWAY_ID_MAX_LEN];
    char gateway_location[GATEWAY_LOCATION_MAX_LEN];
    uint32_t broadcast_end_ms;
} gateway_config_t;

void gateway_config_init(void);
const gateway_config_t *gateway_config_get(void);
bool gateway_config_is_complete(void);

/* Starts the USB Serial/JTAG `cfg` console without exposing generic NVS access. */
void gateway_config_console_start(void);
