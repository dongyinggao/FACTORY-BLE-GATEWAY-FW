#pragma once

#include <stdbool.h>

void network_manager_start(void);
bool network_manager_is_connected(void);
const char *network_manager_status_text(void);
