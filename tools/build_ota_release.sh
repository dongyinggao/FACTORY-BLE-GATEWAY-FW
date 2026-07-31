#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <version> <release-sequence> <https-image-url> <manifest-output>" >&2
    exit 2
fi

release_version=$1
release_sequence=$2
image_url=$3
manifest_output=$4

idf.py -B build/release -DPROJECT_VER="$release_version" build
python3 tools/create_ota_release.py \
    --version "$release_version" \
    --sequence "$release_sequence" \
    --image build/release/ble_gateway.bin \
    --image-url "$image_url" \
    --output "$manifest_output"
