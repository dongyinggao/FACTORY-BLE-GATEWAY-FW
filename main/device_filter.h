#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLE_DEVICE_NAME_MAX_LEN 32

bool device_filter_name_matches(const char *name);
void device_filter_copy_name(char destination[BLE_DEVICE_NAME_MAX_LEN],
                             const uint8_t *source, size_t source_length);
