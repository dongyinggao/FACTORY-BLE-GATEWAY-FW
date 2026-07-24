#include "device_list_model.h"

#include <string.h>

static bool device_list_same_address(const managed_device_t *left, const managed_device_t *right)
{
    return left->report.address_type == right->report.address_type &&
           memcmp(left->report.address, right->report.address, sizeof(left->report.address)) == 0;
}

static bool device_list_is_better(const managed_device_t *candidate, const managed_device_t *current)
{
    if (candidate->online != current->online) {
        return candidate->online;
    }
    return (int32_t)(candidate->last_seen_ms - current->last_seen_ms) > 0;
}

device_list_result_t device_list_model_apply(device_list_model_t *model, const managed_device_t *device)
{
    for (size_t index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        if (model->devices[index].report.name[0] != '\0' &&
            device_list_same_address(&model->devices[index], device)) {
            model->devices[index] = *device;
            return DEVICE_LIST_UPDATED;
        }
    }

    for (size_t index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        if (model->devices[index].report.name[0] == '\0') {
            model->devices[index] = *device;
            return DEVICE_LIST_ADDED;
        }
    }
    return DEVICE_LIST_FULL;
}

size_t device_list_model_count(const device_list_model_t *model)
{
    size_t count = 0;

    for (size_t index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        if (model->devices[index].report.name[0] != '\0') {
            ++count;
        }
    }
    return count;
}

size_t device_list_model_online_count(const device_list_model_t *model)
{
    size_t count = 0;

    for (size_t index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
        if (model->devices[index].report.name[0] != '\0' && model->devices[index].online) {
            ++count;
        }
    }
    return count;
}

const managed_device_t *device_list_model_get_ranked(const device_list_model_t *model, size_t rank)
{
    const managed_device_t *selected[DEVICE_MANAGER_MAX_DEVICES] = {0};

    if (rank >= DEVICE_MANAGER_MAX_DEVICES) {
        return NULL;
    }

    for (size_t position = 0; position <= rank; ++position) {
        const managed_device_t *next = NULL;

        for (size_t index = 0; index < DEVICE_MANAGER_MAX_DEVICES; ++index) {
            const managed_device_t *candidate = &model->devices[index];
            bool already_selected = false;

            if (candidate->report.name[0] == '\0') {
                continue;
            }
            for (size_t selected_index = 0; selected_index < position; ++selected_index) {
                if (candidate == selected[selected_index]) {
                    already_selected = true;
                    break;
                }
            }
            if (already_selected) {
                continue;
            }
            if (next == NULL || device_list_is_better(candidate, next)) {
                next = candidate;
            }
        }
        if (next == NULL) {
            return NULL;
        }
        selected[position] = next;
    }
    return selected[rank];
}
