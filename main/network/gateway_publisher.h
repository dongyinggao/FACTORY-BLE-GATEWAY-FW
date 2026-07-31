#pragma once

#include <stdint.h>

void gateway_publisher_start(void);
uint32_t gateway_publisher_volatile_publish_count(void);
uint32_t gateway_publisher_unrecoverable_drop_count(void);
