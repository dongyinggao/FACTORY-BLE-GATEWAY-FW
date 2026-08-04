#include "ota_manifest_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_whitespace(const char *value)
{
    while (*value != '\0' && isspace((unsigned char)*value)) {
        ++value;
    }
    return value;
}

static bool read_json_string(const char *value, char *output, size_t output_size)
{
    size_t used = 0;

    if (value == NULL || output == NULL || output_size == 0 || *value != '"') {
        return false;
    }
    ++value;
    while (*value != '\0' && *value != '"') {
        if (*value == '\\' || used + 1 >= output_size) {
            return false;
        }
        output[used++] = *value++;
    }
    if (*value != '"' || used == 0) {
        return false;
    }
    output[used] = '\0';
    return true;
}

static const char *find_value(const char *json, const char *key)
{
    char quoted_key[40];
    const char *value;

    if (snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key) >= (int)sizeof(quoted_key)) {
        return NULL;
    }
    value = strstr(json, quoted_key);
    if (value == NULL) {
        return NULL;
    }
    value = skip_whitespace(value + strlen(quoted_key));
    if (*value++ != ':') {
        return NULL;
    }
    return skip_whitespace(value);
}

static bool read_u32(const char *value, uint32_t *output)
{
    char *end = NULL;
    unsigned long number;

    if (value == NULL || !isdigit((unsigned char)*value)) {
        return false;
    }
    number = strtoul(value, &end, 10);
    if (end == value || number == 0 || number > UINT32_MAX) {
        return false;
    }
    *output = (uint32_t)number;
    return true;
}

bool ota_manifest_is_https_url(const char *url)
{
    return url != NULL && strncmp(url, "https://", 8) == 0 && url[8] != '\0';
}

bool ota_manifest_is_valid(const ota_manifest_t *manifest)
{
    size_t index;

    if (manifest == NULL || manifest->schema_version != OTA_MANIFEST_SCHEMA_VERSION ||
        manifest->version[0] == '\0' ||
        !ota_manifest_is_https_url(manifest->image_url) || manifest->image_size == 0 ||
        strlen(manifest->sha256) != OTA_MANIFEST_SHA256_LEN ||
        manifest->hardware_model[0] == '\0' || manifest->idf_target[0] == '\0' ||
        manifest->partition_layout[0] == '\0' || manifest->release_sequence == 0U) {
        return false;
    }
    for (index = 0; index < OTA_MANIFEST_SHA256_LEN; ++index) {
        if (!isxdigit((unsigned char)manifest->sha256[index])) {
            return false;
        }
    }
    return true;
}

bool ota_manifest_parse(const char *json, ota_manifest_t *manifest)
{
    ota_manifest_t candidate = {0};

    if (json == NULL || manifest == NULL ||
        !read_u32(find_value(json, "schema_version"), &candidate.schema_version) ||
        !read_json_string(find_value(json, "version"), candidate.version, sizeof(candidate.version)) ||
        !read_json_string(find_value(json, "image_url"), candidate.image_url, sizeof(candidate.image_url)) ||
        !read_json_string(find_value(json, "sha256"), candidate.sha256, sizeof(candidate.sha256)) ||
        !read_u32(find_value(json, "image_size"), &candidate.image_size) ||
        !read_json_string(find_value(json, "hardware_model"), candidate.hardware_model,
                          sizeof(candidate.hardware_model)) ||
        !read_json_string(find_value(json, "idf_target"), candidate.idf_target,
                          sizeof(candidate.idf_target)) ||
        !read_json_string(find_value(json, "partition_layout"), candidate.partition_layout,
                          sizeof(candidate.partition_layout)) ||
        !read_u32(find_value(json, "release_sequence"), &candidate.release_sequence) ||
        !ota_manifest_is_valid(&candidate)) {
        return false;
    }
    *manifest = candidate;
    return true;
}

bool ota_manifest_is_compatible(const ota_manifest_t *manifest)
{
    return ota_manifest_is_valid(manifest) &&
           strcmp(manifest->hardware_model, OTA_RELEASE_HARDWARE_MODEL) == 0 &&
           strcmp(manifest->idf_target, OTA_RELEASE_IDF_TARGET) == 0 &&
           strcmp(manifest->partition_layout, OTA_RELEASE_PARTITION_LAYOUT) == 0;
}

bool ota_manifest_is_newer_than(const ota_manifest_t *manifest, uint32_t confirmed_sequence)
{
    return ota_manifest_is_compatible(manifest) && manifest->release_sequence > confirmed_sequence;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool ota_manifest_sha256_matches(const char *expected_hex, const uint8_t digest[32])
{
    size_t index;

    if (expected_hex == NULL || digest == NULL || strlen(expected_hex) != OTA_MANIFEST_SHA256_LEN) {
        return false;
    }
    for (index = 0; index < 32; ++index) {
        int high = hex_value(expected_hex[index * 2]);
        int low = hex_value(expected_hex[index * 2 + 1]);
        if (high < 0 || low < 0 || digest[index] != (uint8_t)((high << 4) | low)) {
            return false;
        }
    }
    return true;
}
