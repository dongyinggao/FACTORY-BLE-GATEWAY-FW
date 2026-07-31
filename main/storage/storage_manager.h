#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

void storage_manager_start(void);
bool storage_manager_lock(void);
void storage_manager_unlock(void);
void storage_manager_report_io_failure(int error_code);
void storage_manager_report_full(int error_code);
void storage_manager_report_write_success(void);
bool storage_manager_is_ready(void);
bool storage_manager_is_full(void);
uint32_t storage_manager_generation(void);
const char *storage_manager_status_text(void);
esp_err_t storage_manager_last_error(void);
