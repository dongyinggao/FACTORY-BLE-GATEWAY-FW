#pragma once

#include <stddef.h>

#include "device_manager_core.h"

#define DEVICE_LIST_VISIBLE_ROWS 4

typedef struct {
    managed_device_t devices[DEVICE_MANAGER_MAX_DEVICES];
} device_list_model_t;

typedef enum {
    DEVICE_LIST_ADDED,
    DEVICE_LIST_UPDATED,
    DEVICE_LIST_FULL,
} device_list_result_t;

device_list_result_t device_list_model_apply(device_list_model_t *model, const managed_device_t *device);
size_t device_list_model_count(const device_list_model_t *model);
size_t device_list_model_online_count(const device_list_model_t *model);
const managed_device_t *device_list_model_get_ranked(const device_list_model_t *model, size_t rank);
