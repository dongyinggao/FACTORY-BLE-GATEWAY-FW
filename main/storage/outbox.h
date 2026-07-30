#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OUTBOX_MESSAGE_MAX_LEN 768

void gateway_outbox_init(void);
void gateway_outbox_sync_storage(void);
bool gateway_outbox_is_ready(void);
bool gateway_outbox_enqueue_broadcast(const char *json);
bool gateway_outbox_store_health(const char *json);
bool gateway_outbox_next(char output[OUTBOX_MESSAGE_MAX_LEN], bool *is_health);
void gateway_outbox_ack_current(void);
void gateway_outbox_release_current(void);
uint32_t gateway_outbox_pending_count(void);
uint32_t gateway_outbox_pending_bytes(void);
uint32_t gateway_outbox_failure_count(void);
const char *gateway_outbox_status_text(void);
