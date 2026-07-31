#pragma once

#include <stdbool.h>

/* Registers the generic `sysmem` console alias. */
void system_diagnostics_register_console(void);

/* Prints a point-in-time Heap Caps snapshot. */
void system_diagnostics_print_memory(void);

/* True when task diagnostics were compiled into this firmware. */
bool system_diagnostics_tasks_enabled(void);

/* Prints a task snapshot. Returns false when task diagnostics are disabled. */
bool system_diagnostics_print_tasks(void);
