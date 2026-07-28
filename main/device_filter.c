#include "device_filter.h"

#include <ctype.h>
#include <string.h>

static bool prefix_matches_case_insensitive(const char *name, const char *prefix)
{
    for (size_t index = 0; prefix[index] != '\0'; ++index) {
        if (name[index] == '\0' ||
            tolower((unsigned char)name[index]) != tolower((unsigned char)prefix[index])) {
            return false;
        }
    }
    return true;
}

bool device_filter_name_matches(const char *name)
{
    size_t index = 6;

    if (name == NULL ||
        (!prefix_matches_case_insensitive(name, "SM_ICM") &&
         !prefix_matches_case_insensitive(name, "SM_ICD"))) {
        return false;
    }
    if (!isdigit((unsigned char)name[index])) {
        return false;
    }
    for (; name[index] != '\0'; ++index) {
        if (!isdigit((unsigned char)name[index])) {
            return false;
        }
    }
    return true;
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
