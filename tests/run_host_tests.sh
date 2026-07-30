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

echo "[1/7] device_filter"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/ble/device_filter.c" \
    "${TEST_DIR}/device_filter_test.c" \
    -o "${BUILD_DIR}/device_filter_test"
"${BUILD_DIR}/device_filter_test"

echo "[2/7] device_manager_core"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/device/device_manager_core.c" \
    "${TEST_DIR}/device_manager_test.c" \
    -o "${BUILD_DIR}/device_manager_test"
"${BUILD_DIR}/device_manager_test"

echo "[3/7] device_list_model"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/device/device_list_model.c" \
    "${TEST_DIR}/device_list_model_test.c" \
    -o "${BUILD_DIR}/device_list_model_test"
"${BUILD_DIR}/device_list_model_test"

echo "[4/7] csv_formatter"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/storage/csv_formatter.c" \
    "${TEST_DIR}/time_service_stub.c" \
    "${TEST_DIR}/csv_formatter_test.c" \
    -o "${BUILD_DIR}/csv_formatter_test"
"${BUILD_DIR}/csv_formatter_test"

echo "[5/7] event_json"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/network/event_json.c" \
    "${TEST_DIR}/event_json_test.c" \
    -o "${BUILD_DIR}/event_json_test"
"${BUILD_DIR}/event_json_test"

echo "[6/7] outbox_core"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/storage/outbox_core.c" \
    "${TEST_DIR}/outbox_core_test.c" \
    -o "${BUILD_DIR}/outbox_core_test"
"${BUILD_DIR}/outbox_core_test"

echo "[7/7] publisher_ack"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/network/publisher_ack.c" \
    "${TEST_DIR}/publisher_ack_test.c" \
    -o "${BUILD_DIR}/publisher_ack_test"
"${BUILD_DIR}/publisher_ack_test"

echo "All host tests passed."
