#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_MANIFEST_VERSION_MAX_LEN 32
#define OTA_MANIFEST_URL_MAX_LEN 192
#define OTA_MANIFEST_SHA256_LEN 64
#define OTA_MANIFEST_HARDWARE_MODEL_MAX_LEN 32
#define OTA_MANIFEST_TARGET_MAX_LEN 16
#define OTA_MANIFEST_PARTITION_LAYOUT_MAX_LEN 32
#define OTA_MANIFEST_SCHEMA_VERSION 1U

#define OTA_RELEASE_HARDWARE_MODEL "m5stack-cores3-se"
#define OTA_RELEASE_IDF_TARGET "esp32s3"
#define OTA_RELEASE_PROJECT_NAME "ble_gateway"
#define OTA_RELEASE_PARTITION_LAYOUT "ble-gateway-16m-v1"

typedef struct {
    uint32_t schema_version;
    char version[OTA_MANIFEST_VERSION_MAX_LEN];
    char image_url[OTA_MANIFEST_URL_MAX_LEN];
    char sha256[OTA_MANIFEST_SHA256_LEN + 1];
    uint32_t image_size;
    char hardware_model[OTA_MANIFEST_HARDWARE_MODEL_MAX_LEN];
    char idf_target[OTA_MANIFEST_TARGET_MAX_LEN];
    char partition_layout[OTA_MANIFEST_PARTITION_LAYOUT_MAX_LEN];
    uint32_t release_sequence;
} ota_manifest_t;

/* Parses the deliberately small, release-controlled OTA manifest format. */
bool ota_manifest_parse(const char *json, ota_manifest_t *manifest);
bool ota_manifest_is_valid(const ota_manifest_t *manifest);
bool ota_manifest_is_https_url(const char *url);
bool ota_manifest_sha256_matches(const char *expected_hex, const uint8_t digest[32]);
bool ota_manifest_is_compatible(const ota_manifest_t *manifest);
bool ota_manifest_is_newer_than(const ota_manifest_t *manifest, uint32_t confirmed_sequence);
