#!/usr/bin/env python3
"""Create a release-controlled OTA Manifest from an ESP-IDF application image."""

import argparse
import hashlib
import json
import struct
from pathlib import Path


MANIFEST_SCHEMA_VERSION = 1
HARDWARE_MODEL = "m5stack-cores3-se"
IDF_TARGET = "esp32s3"
PROJECT_NAME = "ble_gateway"
PARTITION_LAYOUT = "ble-gateway-16m-v1"
APP_DESC_OFFSET = 24 + 8
APP_DESC_MAGIC = 0xABCD5432
APP_PARTITION_SIZE = 4 * 1024 * 1024


def read_c_string(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("ascii", errors="strict")


def read_image_descriptor(image: bytes) -> tuple[str, str]:
    if len(image) < APP_DESC_OFFSET + 80:
        raise ValueError("image is too small to contain an ESP-IDF app descriptor")
    descriptor = image[APP_DESC_OFFSET:]
    magic, = struct.unpack_from("<I", descriptor, 0)
    if magic != APP_DESC_MAGIC:
        raise ValueError("image does not contain a valid ESP-IDF app descriptor")
    return read_c_string(descriptor[16:48]), read_c_string(descriptor[48:80])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Release version embedded in the application image")
    parser.add_argument("--sequence", required=True, type=int, help="Strictly increasing release sequence")
    parser.add_argument("--image", default="build/ble_gateway.bin", type=Path)
    parser.add_argument("--image-url", required=True, help="HTTPS URL of the exact image")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    if args.sequence <= 0:
        parser.error("--sequence must be greater than zero")
    if not args.image_url.startswith("https://"):
        parser.error("--image-url must use https://")

    image = args.image.read_bytes()
    if len(image) > APP_PARTITION_SIZE:
        parser.error(f"image is {len(image)} B; OTA partition limit is {APP_PARTITION_SIZE} B")
    image_version, project_name = read_image_descriptor(image)
    if image_version != args.version:
        parser.error(f"image version is {image_version!r}, not requested {args.version!r}")
    if project_name != PROJECT_NAME:
        parser.error(f"image project is {project_name!r}, expected {PROJECT_NAME!r}")

    manifest = {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "version": args.version,
        "release_sequence": args.sequence,
        "hardware_model": HARDWARE_MODEL,
        "idf_target": IDF_TARGET,
        "partition_layout": PARTITION_LAYOUT,
        "image_url": args.image_url,
        "image_size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {args.output}: version={args.version}, sequence={args.sequence}, size={len(image)}")


if __name__ == "__main__":
    main()
