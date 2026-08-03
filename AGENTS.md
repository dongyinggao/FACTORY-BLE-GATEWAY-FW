# Repository Guidelines

## Project Structure & Module Organization

This is an ESP-IDF 5.5.5 firmware project for the ESP32-S3-based M5Stack
CoreS3-SE BLE gateway. Application code belongs in `main/`; register every
new `.c` source in `main/CMakeLists.txt`. Keep a module's public API in a
same-named header, for example `device_manager.c` and `device_manager.h`.

Organize source by responsibility: `main/app/` contains the entry point and
LVGL UI, `main/ble/` scanning and name filtering, `main/device/` lifecycle
management, `main/storage/` CSV and Outbox, `main/config/` NVS configuration,
and `main/network/` Wi-Fi, SNTP, MQTT, and publishing.

`ble_scanner` owns NimBLE scanning, `device_filter` contains hardware-free
advertisement-name filtering, `device_manager` owns deduplication and device
state, and `app_ui` is the only module that updates LVGL. `tests/` contains
host-side C tests. `partitions/v1/16m.csv` is the 16 MiB flash layout.
Configuration defaults live in `sdkconfig.defaults`; do not edit generated
files under `build/` or managed dependencies under `managed_components/`.
Planning and hardware notes are in `doc/`.

## Build, Test, and Development Commands

Activate the intended ESP-IDF environment before running commands:

```bash
source /home/sm-dawn/.espressif/v5.5.5/esp-idf/export.sh
idf.py build
```

Build output is `build/ble_gateway.bin`. Use `idf.py -p /dev/ttyACM0 flash
monitor` only with connected hardware. Run all host-side unit tests without
hardware:

```bash
./tests/run_host_tests.sh
```

Avoid `idf.py fullclean` unless necessary: it may try to reconcile managed
components. Never modify generated content to fix a build.

## Coding Style & Naming Conventions

Use C17-compatible C, four-space indentation, braces on their own line, and
`snake_case` for functions and variables. Use module-prefixed public types
and constants such as `ble_scan_report_t` and `DEVICE_MANAGER_MAX_DEVICES`.
Make helpers `static`. Return events through FreeRTOS queues; NimBLE callbacks
must not call LVGL. Acquire `bsp_display_lock()` around all LVGL updates.

## Testing Guidelines

Add a host test named `tests/<module>_test.c` for pure logic. Cover positive,
negative, capacity, and timeout cases. Hardware validation is manual: verify
touch control, active scanning, UI updates, and serial logs on a CoreS3-SE.

## Commit & Pull Request Guidelines

Recent history uses short Chinese imperative summaries, for example `添加自定义flash分区表`.
Keep commits focused and describe the subsystem affected. Every commit must include a body with
`Why` (problem or motivation), `What` (scope of changes), and `How` (implementation approach and
verification); use this format even for small fixes. PRs should state
the target board, configuration changes, build/test commands run, and any
hardware-only checks still pending. Include LCD screenshots or serial logs
when a UI or BLE behavior changes.
