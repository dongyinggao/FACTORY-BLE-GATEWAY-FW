#include "device_filter.h"

#include <string.h>

bool device_filter_name_matches(const char *name)
{
    return name != NULL &&
           (strncmp(name, "ICD", 3) == 0 || strncmp(name, "ICM", 3) == 0);
}

void device_filter_copy_name(char destination[BLE_DEVICE_NAME_MAX_LEN],
                             const uint8_t *source, size_t source_length)
{
    size_t length = source_length;

    if (length >= BLE_DEVICE_NAME_MAX_LEN) {
        length = BLE_DEVICE_NAME_MAX_LEN - 1;
    }

    if (source != NULL && length > 0) {
        memcpy(destination, source, length);
    }
    destination[length] = '\0';
}
