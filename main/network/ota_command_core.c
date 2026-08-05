#include "ota_command_core.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static size_t ota_command_bounded_length(const char *value, size_t capacity)
{
    size_t length = 0U;

    while (length < capacity && value[length] != '\0') {
        ++length;
    }
    return length;
}

static bool ota_command_id_is_valid(const char *value, size_t capacity)
{
    size_t length;

    if (value == NULL) {
        return false;
    }
    length = ota_command_bounded_length(value, capacity);
    if (length == 0U || length == capacity) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        unsigned char ch = (unsigned char)value[index];

        if (!isalnum(ch) && ch != '-' && ch != '_' && ch != '.' && ch != ':') {
            return false;
        }
    }
    return true;
}

bool ota_command_is_valid(const ota_command_t *command)
{
    if (command == NULL || command->expires_at_epoch_s == 0U) {
        return false;
    }
    return ota_command_id_is_valid(command->command_id, sizeof(command->command_id)) &&
           ota_command_id_is_valid(command->campaign_id, sizeof(command->campaign_id)) &&
           strncmp(command->manifest_uri, "https://", 8U) == 0 &&
           command->manifest_uri[8] != '\0' &&
           ota_command_bounded_length(command->manifest_uri, sizeof(command->manifest_uri)) <
               sizeof(command->manifest_uri);
}

bool ota_command_is_expired(const ota_command_t *command, uint32_t now_epoch_s)
{
    return command == NULL || command->expires_at_epoch_s <= now_epoch_s;
}
