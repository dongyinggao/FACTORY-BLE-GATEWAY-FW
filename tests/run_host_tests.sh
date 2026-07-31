#!/usr/bin/env bash
set -euo pipefail

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${TEST_DIR}/.." && pwd)"
CC="${CC:-gcc}"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/blegateway-tests.XXXXXX")"

cleanup() {
    rm -rf "${BUILD_DIR}"
}
trap cleanup EXIT

COMMON_FLAGS=(
    -std=c99 -Wall -Wextra -Werror
    -I "${PROJECT_DIR}/main/app"
    -I "${PROJECT_DIR}/main/ble"
    -I "${PROJECT_DIR}/main/device"
    -I "${PROJECT_DIR}/main/storage"
    -I "${PROJECT_DIR}/main/config"
    -I "${PROJECT_DIR}/main/network"
)

echo "[1/9] device_filter"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/ble/device_filter.c" \
    "${TEST_DIR}/device_filter_test.c" \
    -o "${BUILD_DIR}/device_filter_test"
"${BUILD_DIR}/device_filter_test"

echo "[2/9] device_manager_core"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/device/device_manager_core.c" \
    "${TEST_DIR}/device_manager_test.c" \
    -o "${BUILD_DIR}/device_manager_test"
"${BUILD_DIR}/device_manager_test"

echo "[3/9] device_list_model"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/device/device_list_model.c" \
    "${TEST_DIR}/device_list_model_test.c" \
    -o "${BUILD_DIR}/device_list_model_test"
"${BUILD_DIR}/device_list_model_test"

echo "[4/9] csv_formatter"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/storage/csv_formatter.c" \
    "${TEST_DIR}/time_service_stub.c" \
    "${TEST_DIR}/csv_formatter_test.c" \
    -o "${BUILD_DIR}/csv_formatter_test"
"${BUILD_DIR}/csv_formatter_test"

echo "[5/9] event_json"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/network/event_json.c" \
    "${TEST_DIR}/event_json_test.c" \
    -o "${BUILD_DIR}/event_json_test"
"${BUILD_DIR}/event_json_test"

echo "[6/9] outbox_core"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/storage/outbox_core.c" \
    "${TEST_DIR}/outbox_core_test.c" \
    -o "${BUILD_DIR}/outbox_core_test"
"${BUILD_DIR}/outbox_core_test"

echo "[7/9] publisher_ack"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/network/publisher_ack.c" \
    "${TEST_DIR}/publisher_ack_test.c" \
    -o "${BUILD_DIR}/publisher_ack_test"
"${BUILD_DIR}/publisher_ack_test"

echo "[8/9] storage_state_core"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/storage/storage_state_core.c" \
    "${TEST_DIR}/storage_state_core_test.c" \
    -o "${BUILD_DIR}/storage_state_core_test"
"${BUILD_DIR}/storage_state_core_test"

echo "[9/9] 128_device_stress"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/ble/device_filter.c" \
    "${PROJECT_DIR}/main/device/device_manager_core.c" \
    "${PROJECT_DIR}/main/storage/csv_formatter.c" \
    "${PROJECT_DIR}/main/storage/outbox_core.c" \
    "${PROJECT_DIR}/main/network/event_json.c" \
    "${PROJECT_DIR}/main/network/publisher_ack.c" \
    "${TEST_DIR}/time_service_stub.c" \
    "${TEST_DIR}/stress_128_devices_test.c" \
    -o "${BUILD_DIR}/stress_128_devices_test"
"${BUILD_DIR}/stress_128_devices_test"

echo "All host tests passed."
