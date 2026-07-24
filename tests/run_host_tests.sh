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

COMMON_FLAGS=(-std=c99 -Wall -Wextra -Werror -I "${PROJECT_DIR}/main")

echo "[1/2] device_filter"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/device_filter.c" \
    "${TEST_DIR}/device_filter_test.c" \
    -o "${BUILD_DIR}/device_filter_test"
"${BUILD_DIR}/device_filter_test"

echo "[2/2] device_manager_core"
"${CC}" "${COMMON_FLAGS[@]}" \
    "${PROJECT_DIR}/main/device_manager_core.c" \
    "${TEST_DIR}/device_manager_test.c" \
    -o "${BUILD_DIR}/device_manager_test"
"${BUILD_DIR}/device_manager_test"

echo "All host tests passed."
