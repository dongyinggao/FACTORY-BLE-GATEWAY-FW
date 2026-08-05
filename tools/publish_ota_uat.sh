#!/usr/bin/env bash
# Build, package, upload, and activate one UAT OTA release.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/publish_ota_uat.sh <version> <release-sequence> [ota-host]

Builds a release image, generates its Manifest, uploads both files to the UAT
server, and activates manifest.json only after the versioned files are ready.

Defaults:
  ota-host:       ble-gateway-uat.singularmedical.net
  SSH target:     ble_gateway@192.168.19.21 (UAT management address)
  remote storage: /srv/ble-gateway-data/ota

Optional environment variables:
  OTA_SSH_TARGET   Override the SSH target, for example user@host.
  OTA_REMOTE_DIR   Override the server-side OTA directory.

The script never stores an SSH or sudo password. SSH and sudo may prompt
interactively according to the server account policy.
EOF
}

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    usage >&2
    exit 2
fi

release_version=$1
release_sequence=$2
ota_host=${3:-ble-gateway-uat.singularmedical.net}

if [[ ! "$release_version" =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]]; then
    echo "Invalid release version: $release_version" >&2
    exit 2
fi
if [[ ! "$release_sequence" =~ ^[1-9][0-9]*$ ]]; then
    echo "Release sequence must be a positive integer: $release_sequence" >&2
    exit 2
fi
if [[ ! "$ota_host" =~ ^[A-Za-z0-9.-]+$ ]]; then
    echo "Invalid OTA host: $ota_host" >&2
    exit 2
fi

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
idf_export=${IDF_EXPORT:-/home/sm-dawn/.espressif/v5.5.5/esp-idf/export.sh}
# HTTPS must retain the DNS name because it is covered by the OTA certificate.
# UAT SSH uses the internal management address so a VPN/split-DNS answer for
# ota_host cannot redirect the release upload to an unrelated endpoint.
ota_ssh_target=${OTA_SSH_TARGET:-ble_gateway@192.168.19.21}
ota_remote_dir=${OTA_REMOTE_DIR:-/srv/ble-gateway-data/ota}
image_name="ble_gateway-${release_version}.bin"
manifest_name="manifest-${release_version}.json"
image_url="https://${ota_host}/ota/${image_name}"
remote_tmp="/tmp/ble_gateway_ota_${release_version}_${release_sequence}_$$"

if [ ! -f "$idf_export" ]; then
    echo "ESP-IDF export script not found: $idf_export" >&2
    exit 1
fi

cd "$project_root"
source "$idf_export"

echo "[0/5] Checking SSH access to ${ota_ssh_target}"
ssh -o ConnectTimeout=10 "$ota_ssh_target" "true"

echo "[1/5] Building release ${release_version} (sequence ${release_sequence})"
./tools/build_ota_release.sh "$release_version" "$release_sequence" "$image_url" "ota/${manifest_name}"

echo "[2/5] Staging local release artifacts"
install -m 0644 "build/release/ble_gateway.bin" "ota/${image_name}"
cat > "ota/index.html" <<EOF
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Factory BLE Gateway OTA</title>
</head>
<body>
  <h1>Factory BLE Gateway OTA Release</h1>
  <p>当前发布版本：${release_version}（序列号 ${release_sequence}）</p>
  <ul>
    <li><a href="manifest.json">当前 Manifest（${release_version}）</a></li>
    <li><a href="${manifest_name}">版本 Manifest（${release_version}）</a></li>
    <li><a href="${image_name}">固件镜像（${release_version}）</a></li>
  </ul>
  <p>仅存放已发布的固件和 Manifest，不上传配置、日志或凭据。</p>
</body>
</html>
EOF

echo "[3/5] Uploading versioned image and Manifest to ${ota_ssh_target}"
ssh "$ota_ssh_target" "set -eu; rm -rf '${remote_tmp}'; mkdir -p '${remote_tmp}'"
scp "ota/${image_name}" "ota/${manifest_name}" "ota/index.html" "${ota_ssh_target}:${remote_tmp}/"

echo "[4/5] Publishing versioned files and activating manifest.json"
ssh "$ota_ssh_target" "set -eu
    sudo install -o ble_gateway -g www-data -m 0640 '${remote_tmp}/${image_name}' '${ota_remote_dir}/${image_name}'
    sudo install -o ble_gateway -g www-data -m 0640 '${remote_tmp}/${manifest_name}' '${ota_remote_dir}/${manifest_name}'
    sudo install -o ble_gateway -g www-data -m 0640 '${remote_tmp}/index.html' '${ota_remote_dir}/index.html'
    sudo install -o ble_gateway -g www-data -m 0640 '${remote_tmp}/${manifest_name}' '${ota_remote_dir}/manifest.json'
    rm -rf '${remote_tmp}'
    sha256sum '${ota_remote_dir}/${image_name}'
    echo 'Active Manifest:'
    sudo sed -n '1,120p' '${ota_remote_dir}/manifest.json'"

echo "[5/5] Release active"
echo "Manifest: https://${ota_host}/ota/manifest.json"
echo "Image:    ${image_url}"
echo "On the gateway, run: ota check && ota status"
