#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "csv_formatter.h"
#include "device_filter.h"
#include "device_manager_core.h"
#include "event_json.h"
#include "outbox_core.h"
#include "publisher_ack.h"

#define STRESS_DEVICE_COUNT DEVICE_MANAGER_MAX_DEVICES
#define UI_QUEUE_CAPACITY 32U
#define LIFECYCLE_QUEUE_CAPACITY 256U

typedef struct {
    uint32_t capacity;
    uint32_t depth;
    uint32_t high_watermark;
    uint32_t dropped;
} simulated_queue_t;

static bool simulated_queue_send(simulated_queue_t *queue)
{
    if (queue->depth == queue->capacity) {
        ++queue->dropped;
        return false;
    }
    ++queue->depth;
    if (queue->depth > queue->high_watermark) {
        queue->high_watermark = queue->depth;
    }
    return true;
}

static void simulated_queue_drain(simulated_queue_t *queue)
{
    queue->depth = 0;
}

static ble_scan_report_t report_for(unsigned int index, uint8_t address_type, int8_t rssi)
{
    ble_scan_report_t report = {0};

    snprintf(report.name, sizeof(report.name), index % 2U == 0U ? "SM_ICM%u" : "SM_ICD%u", index);
    report.address[0] = (uint8_t)index;
    report.address[1] = (uint8_t)(index >> 3U);
    report.address[2] = 0xA5;
    report.address[3] = 0x5A;
    report.address[4] = 0x01;
    report.address[5] = 0xC0;
    report.address_type = address_type;
    report.rssi = rssi;
    return report;
}

static void lifecycle_from_device(device_lifecycle_event_t *event, const managed_device_t *device,
                                  device_lifecycle_event_type_t type)
{
    memset(event, 0, sizeof(*event));
    event->type = type;
    snprintf(event->broadcast_id, sizeof(event->broadcast_id), "STRESS-%u",
             (unsigned int)device->report.address[0]);
    memcpy(event->name, device->report.name, sizeof(event->name));
    memcpy(event->address, device->report.address, sizeof(event->address));
    event->rssi = device->report.rssi;
    event->broadcast_started_ms = device->broadcast_started_ms;
    event->last_seen_ms = device->last_seen_ms;
    event->end_detected_ms = device->end_detected_ms;
    event->broadcast_started_wall_ms = device->broadcast_started_wall_ms;
    event->last_seen_wall_ms = device->last_seen_wall_ms;
    event->end_detected_wall_ms = device->end_detected_wall_ms;
}

static void test_128_device_lifecycle_and_fanout(void)
{
    device_registry_t registry = {0};
    gateway_config_t config = {.gateway_id = "STRESS-GW", .gateway_location = "Host-Test"};
    simulated_queue_t ui = {.capacity = UI_QUEUE_CAPACITY};
    simulated_queue_t capture = {.capacity = LIFECYCLE_QUEUE_CAPACITY};
    simulated_queue_t upload = {.capacity = LIFECYCLE_QUEUE_CAPACITY};
    gateway_outbox_core_t outbox;
    gateway_publisher_ack_t ack;
    unsigned int started = 0;
    unsigned int ended = 0;
    size_t device_index;
    char event_ids[STRESS_DEVICE_COUNT * 2U][GATEWAY_EVENT_ID_MAX_LEN];

    gateway_outbox_core_init(&outbox, 512U * 1024U);
    gateway_publisher_ack_reset(&ack);

    for (unsigned int index = 0; index < STRESS_DEVICE_COUNT; ++index) {
        ble_scan_report_t report = report_for(index, 1, (int8_t)(-35 - (index % 55U)));
        device_lifecycle_event_t event;
        char csv[CSV_LIFECYCLE_LINE_MAX_LEN];

        assert(device_filter_name_matches(report.name));
        assert(device_registry_process_report(&registry, &report, 1000U + index,
                                              1000U + index, &device_index) == DEVICE_REGISTRY_ADDED);
        lifecycle_from_device(&event, &registry.devices[device_index],
                              DEVICE_LIFECYCLE_BROADCAST_STARTED);
        assert(csv_format_lifecycle_event(csv, sizeof(csv), &event, &config) > 0);
        assert(simulated_queue_send(&ui));
        simulated_queue_drain(&ui);
        assert(simulated_queue_send(&capture));
        assert(simulated_queue_send(&upload));
        gateway_event_id_make(event_ids[started], sizeof(event_ids[started]), 0x12800000U, started + 1U);
        gateway_outbox_core_record_append(&outbox, 240U);
        ++started;
    }
    assert(started == STRESS_DEVICE_COUNT);
    assert(ui.high_watermark == 1U);
    assert(capture.high_watermark == STRESS_DEVICE_COUNT);
    assert(upload.high_watermark == STRESS_DEVICE_COUNT);
    assert(ui.dropped == 0U && capture.dropped == 0U && upload.dropped == 0U);

    for (unsigned int index = 0; index < STRESS_DEVICE_COUNT; ++index) {
        ble_scan_report_t report = report_for(index, 2, (int8_t)(-40 - (index % 45U)));

        assert(device_registry_process_report(&registry, &report, 3000U + index,
                                              3000U + index, &device_index) == DEVICE_REGISTRY_UPDATED);
        assert(device_index == index);
    }

    simulated_queue_drain(&ui);
    simulated_queue_drain(&capture);
    simulated_queue_drain(&upload);
    while (device_registry_mark_next_broadcast_ended(&registry, 9000U, 9000U, 5000U,
                                                      &device_index) == DEVICE_REGISTRY_BROADCAST_ENDED) {
        device_lifecycle_event_t event;
        char csv[CSV_LIFECYCLE_LINE_MAX_LEN];

        lifecycle_from_device(&event, &registry.devices[device_index],
                              DEVICE_LIFECYCLE_BROADCAST_ENDED);
        assert(csv_format_lifecycle_event(csv, sizeof(csv), &event, &config) > 0);
        assert(simulated_queue_send(&ui));
        simulated_queue_drain(&ui);
        assert(simulated_queue_send(&capture));
        assert(simulated_queue_send(&upload));
        gateway_event_id_make(event_ids[started + ended], sizeof(event_ids[started + ended]),
                              0x12800000U, started + ended + 1U);
        gateway_outbox_core_record_append(&outbox, 240U);
        ++ended;
    }
    assert(ended == STRESS_DEVICE_COUNT);
    assert(outbox.pending_messages == STRESS_DEVICE_COUNT * 2U);
    assert(strcmp(event_ids[0], event_ids[STRESS_DEVICE_COUNT]) != 0);

    /* A stalled UI consumer accepts 32 records and drops the remaining 96. */
    for (unsigned int index = 0; index < STRESS_DEVICE_COUNT; ++index) {
        (void)simulated_queue_send(&ui);
    }
    assert(ui.depth == UI_QUEUE_CAPACITY);
    assert(ui.dropped == STRESS_DEVICE_COUNT - UI_QUEUE_CAPACITY);
    assert(capture.dropped == 0U && upload.dropped == 0U);

    /* Hold capture/upload consumers as well. They already contain the 128
     * ended events; one more 128-record wave fills them, then another wave
     * must be rejected. */
    for (unsigned int index = 0; index < STRESS_DEVICE_COUNT; ++index) {
        assert(simulated_queue_send(&capture));
        assert(simulated_queue_send(&upload));
    }
    assert(capture.depth == LIFECYCLE_QUEUE_CAPACITY);
    assert(upload.depth == LIFECYCLE_QUEUE_CAPACITY);
    for (unsigned int index = 0; index < STRESS_DEVICE_COUNT; ++index) {
        assert(!simulated_queue_send(&capture));
        assert(!simulated_queue_send(&upload));
    }
    assert(capture.dropped == STRESS_DEVICE_COUNT);
    assert(upload.dropped == STRESS_DEVICE_COUNT);

    for (unsigned int index = 0; index < STRESS_DEVICE_COUNT; ++index) {
        assert(gateway_publisher_ack_begin(&ack, (int)index + 1));
        assert(!gateway_publisher_ack_accept(&ack, (int)index + 2));
        assert(gateway_publisher_ack_accept(&ack, (int)index + 1));
        gateway_outbox_core_ack_broadcast(&outbox);
    }
    assert(outbox.pending_messages == STRESS_DEVICE_COUNT);

    {
        ble_scan_report_t extra = report_for(STRESS_DEVICE_COUNT, 1, -60);
        assert(device_registry_process_report(&registry, &extra, 10000U, 10000U,
                                              &device_index) == DEVICE_REGISTRY_FULL);
    }
}

int main(void)
{
    test_128_device_lifecycle_and_fanout();
    puts("128-device stress tests passed");
    return 0;
}
