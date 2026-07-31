#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_manifest_core.h"

static const char *valid_manifest =
    "{\"schema_version\":1,\"version\":\"1.2.3\","
    "\"image_url\":\"https://updates.example/ble_gateway.bin\",\"image_size\":1234567,"
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
    "\"hardware_model\":\"m5stack-cores3-se\",\"idf_target\":\"esp32s3\","
    "\"partition_layout\":\"ble-gateway-16m-v1\",\"release_sequence\":7}";

static void test_parse_valid_manifest(void)
{
    ota_manifest_t manifest;

    assert(ota_manifest_parse(valid_manifest, &manifest));
    assert(strcmp(manifest.version, "1.2.3") == 0);
    assert(manifest.image_size == 1234567U);
    assert(manifest.release_sequence == 7U);
    assert(ota_manifest_is_https_url(manifest.image_url));
    assert(ota_manifest_is_compatible(&manifest));
    assert(ota_manifest_is_newer_than(&manifest, 6U));
    assert(!ota_manifest_is_newer_than(&manifest, 7U));
}

static void test_reject_insecure_or_incomplete_manifest(void)
{
    ota_manifest_t manifest;
    const char *http_manifest =
        "{\"schema_version\":1,\"version\":\"1\",\"image_url\":\"http://example/fw.bin\","
        "\"image_size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
        "\"hardware_model\":\"m5stack-cores3-se\",\"idf_target\":\"esp32s3\","
        "\"partition_layout\":\"ble-gateway-16m-v1\",\"release_sequence\":1}";

    assert(!ota_manifest_parse(http_manifest, &manifest));
    assert(!ota_manifest_parse("{\"version\":\"1\"}", &manifest));
}

static void test_reject_incompatible_manifest(void)
{
    ota_manifest_t manifest;
    char incompatible[512];

    snprintf(incompatible, sizeof(incompatible), "%s", valid_manifest);
    assert(ota_manifest_parse(incompatible, &manifest));
    snprintf(manifest.hardware_model, sizeof(manifest.hardware_model), "other-board");
    assert(!ota_manifest_is_compatible(&manifest));
}

static void test_sha256_comparison(void)
{
    const char *hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    uint8_t digest[32];

    for (size_t index = 0; index < sizeof(digest); ++index) {
        digest[index] = (uint8_t)index;
    }
    assert(ota_manifest_sha256_matches(hex, digest));
    digest[31] = 0;
    assert(!ota_manifest_sha256_matches(hex, digest));
}

int main(void)
{
    test_parse_valid_manifest();
    test_reject_insecure_or_incomplete_manifest();
    test_reject_incompatible_manifest();
    test_sha256_comparison();
    return 0;
}
