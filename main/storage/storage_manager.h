#pragma once

#include <stdbool.h>
#include <stdint.h>

void storage_manager_start(void);
bool storage_manager_lock(void);
void storage_manager_unlock(void);
void storage_manager_report_io_failure(void);
bool storage_manager_is_ready(void);
uint32_t storage_manager_generation(void);
