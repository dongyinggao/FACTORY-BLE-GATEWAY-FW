#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ota_command_core.h"

static ota_command_t make_valid_command(void)
{
    ota_command_t command = {0};

    snprintf(command.command_id, sizeof(command.command_id), "cmd-20260803-001");
    snprintf(command.campaign_id, sizeof(command.campaign_id), "pilot.room101");
    snprintf(command.manifest_uri, sizeof(command.manifest_uri),
             "https://ota.example.com/releases/manifest-1.0.1.json");
    command.expires_at_epoch_s = 1780000000U;
    return command;
}

int main(void)
{
    ota_command_t command = make_valid_command();

    assert(ota_command_is_valid(&command));
    assert(!ota_command_is_expired(&command, 1779999999U));
    assert(ota_command_is_expired(&command, 1780000000U));

    command.command_id[3] = ' ';
    assert(!ota_command_is_valid(&command));
    command = make_valid_command();
    snprintf(command.manifest_uri, sizeof(command.manifest_uri), "http://ota.example.com/manifest.json");
    assert(!ota_command_is_valid(&command));
    command = make_valid_command();
    command.campaign_id[0] = '\0';
    assert(!ota_command_is_valid(&command));
    command = make_valid_command();
    command.expires_at_epoch_s = 0U;
    assert(!ota_command_is_valid(&command));

    puts("ota_command_core tests passed");
    return 0;
}
