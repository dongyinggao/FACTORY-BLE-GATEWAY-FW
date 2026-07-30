#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLE_DEVICE_NAME_MAX_LEN 32

bool device_filter_name_matches(const char *name);
bool device_filter_set_rules(const char *rules);
const char *device_filter_get_rules(void);
void device_filter_copy_name(char destination[BLE_DEVICE_NAME_MAX_LEN],
                             const uint8_t *source, size_t source_length);
