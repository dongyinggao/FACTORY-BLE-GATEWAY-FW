#include "device_filter.h"

#include <ctype.h>
#include <string.h>

#define DEVICE_FILTER_RULES_MAX_LEN 128
#define DEVICE_FILTER_DEFAULT_RULES "SM_ICM*,SM_ICD*"

static char active_rules[DEVICE_FILTER_RULES_MAX_LEN] = DEVICE_FILTER_DEFAULT_RULES;

static bool prefix_matches_case_insensitive(const char *name, const char *prefix, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        if (name[index] == '\0' ||
            tolower((unsigned char)name[index]) != tolower((unsigned char)prefix[index])) {
            return false;
        }
    }
    return true;
}

bool device_filter_name_matches(const char *name)
{
    const char *rule;

    if (name == NULL) {
        return false;
    }
    rule = active_rules;
    while (*rule != '\0') {
        const char *end = strchr(rule, ',');
        size_t prefix_length = end == NULL ? strlen(rule) : (size_t)(end - rule);
        size_t index;

        if (prefix_length >= 2 && rule[prefix_length - 1] == '*') {
            --prefix_length;
            if (prefix_matches_case_insensitive(name, rule, prefix_length)) {
                for (index = prefix_length; name[index] != '\0'; ++index) {
                    if (!isdigit((unsigned char)name[index])) {
                        break;
                    }
                }
                if (index > prefix_length && name[index] == '\0') {
                    return true;
                }
            }
        }
        if (end == NULL) break;
        rule = end + 1;
    }
    return false;
}

bool device_filter_set_rules(const char *rules)
{
    const char *item;
    if (rules == NULL || rules[0] == '\0' || strlen(rules) >= sizeof(active_rules)) {
        strcpy(active_rules, DEVICE_FILTER_DEFAULT_RULES);
        return false;
    }
    for (item = rules; *item != '\0'; ) {
        const char *end = strchr(item, ',');
        size_t length = end == NULL ? strlen(item) : (size_t)(end - item);
        if (length < 2 || item[length - 1] != '*') {
            strcpy(active_rules, DEVICE_FILTER_DEFAULT_RULES);
            return false;
        }
        item = end == NULL ? item + length : end + 1;
    }
    strcpy(active_rules, rules);
    return true;
}

const char *device_filter_get_rules(void)
{
    return active_rules;
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
