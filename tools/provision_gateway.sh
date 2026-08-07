#!/usr/bin/env bash
# Configure a new CoreS3-SE gateway through its USB Serial/JTAG console.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./tools/provision_gateway.sh <serial-port>

Example:
  ./tools/provision_gateway.sh /dev/ttyACM0

The Wi-Fi password is requested without echo. For a controlled production
station it may instead be supplied through GATEWAY_WIFI_PASSWORD. Other
settings can be overridden with GATEWAY_ID, GATEWAY_LOCATION, BCAST_END_S,
WIFI_SSID, MQTT_URI, MQTT_USERNAME, MQTT_PASSWORD, MQTT_QOS, NTP_SERVER,
TIMEZONE, and OTA_MANIFEST_URI. MQTT credentials are written only when the
corresponding environment variables are supplied.
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
    usage
    exit 0
fi

PORT=${1:?"serial port is required; use --help for usage"}
if [[ ! -c "$PORT" ]]; then
    printf 'error: %s is not a serial device\n' "$PORT" >&2
    exit 1
fi

GATEWAY_ID=${GATEWAY_ID:-GW-01}
GATEWAY_LOCATION=${GATEWAY_LOCATION:-Room101-North}
BCAST_END_S=${BCAST_END_S:-5}
WIFI_SSID=${WIFI_SSID:-singularmedical-guest}
# UAT Mosquitto is hosted with the OTA/web server. Keep this as the current
# test default; production provisioning must override it with mqtts:// URI.
MQTT_URI=${MQTT_URI:-mqtt://192.168.19.21:1883}
MQTT_USERNAME=${MQTT_USERNAME:-}
MQTT_QOS=${MQTT_QOS:-1}
NTP_SERVER=${NTP_SERVER:-ntp.aliyun.com}
TIMEZONE=${TIMEZONE:-CST-8}
OTA_MANIFEST_URI=${OTA_MANIFEST_URI:-https://ble-gateway-uat.singularmedical.net/ota/manifest.json}

if [[ -z ${GATEWAY_WIFI_PASSWORD:-} ]]; then
    read -r -s -p "Wi-Fi password for ${WIFI_SSID}: " GATEWAY_WIFI_PASSWORD
    printf '\n'
fi
if [[ -z $GATEWAY_WIFI_PASSWORD ]]; then
    printf 'error: Wi-Fi password must not be empty\n' >&2
    exit 1
fi

PYTHON_BIN=${IDF_PYTHON_ENV_PATH:+$IDF_PYTHON_ENV_PATH/bin/python}
PYTHON_BIN=${PYTHON_BIN:-python3}
if ! "$PYTHON_BIN" -c 'import serial' >/dev/null 2>&1; then
    printf 'error: pyserial is unavailable. Source ESP-IDF export.sh, then retry.\n' >&2
    exit 1
fi

printf 'Configuring %s: gateway=%s, location=%s, Wi-Fi=%s, MQTT=%s\n' \
       "$PORT" "$GATEWAY_ID" "$GATEWAY_LOCATION" "$WIFI_SSID" "$MQTT_URI"
printf 'Do not keep another serial monitor connected while this script runs.\n'

export PROVISION_PORT="$PORT"
export PROVISION_GATEWAY_ID="$GATEWAY_ID"
export PROVISION_GATEWAY_LOCATION="$GATEWAY_LOCATION"
export PROVISION_BCAST_END_S="$BCAST_END_S"
export PROVISION_WIFI_SSID="$WIFI_SSID"
export PROVISION_WIFI_PASSWORD="$GATEWAY_WIFI_PASSWORD"
export PROVISION_MQTT_URI="$MQTT_URI"
export PROVISION_MQTT_USERNAME="$MQTT_USERNAME"
export PROVISION_MQTT_QOS="$MQTT_QOS"
export PROVISION_NTP_SERVER="$NTP_SERVER"
export PROVISION_TIMEZONE="$TIMEZONE"
export PROVISION_OTA_MANIFEST_URI="$OTA_MANIFEST_URI"
if [[ -v MQTT_PASSWORD ]]; then
    export PROVISION_MQTT_PASSWORD="$MQTT_PASSWORD"
fi

"$PYTHON_BIN" - <<'PY'
import os
import sys
import time

import serial

password = os.environ["PROVISION_WIFI_PASSWORD"]
commands = [
    ("gateway_id", os.environ["PROVISION_GATEWAY_ID"]),
    ("gateway_loc", os.environ["PROVISION_GATEWAY_LOCATION"]),
    ("bcast_end_s", os.environ["PROVISION_BCAST_END_S"]),
    ("wifi_ssid", os.environ["PROVISION_WIFI_SSID"]),
    ("wifi_password", password),
    ("mqtt_uri", os.environ["PROVISION_MQTT_URI"]),
    ("mqtt_qos", os.environ["PROVISION_MQTT_QOS"]),
    ("ntp_server", os.environ["PROVISION_NTP_SERVER"]),
    ("timezone", os.environ["PROVISION_TIMEZONE"]),
    ("ota_manifest_uri", os.environ["PROVISION_OTA_MANIFEST_URI"]),
]
if os.environ.get("PROVISION_MQTT_USERNAME"):
    commands.append(("mqtt_username", os.environ["PROVISION_MQTT_USERNAME"]))
if "PROVISION_MQTT_PASSWORD" in os.environ:
    commands.append(("mqtt_password", os.environ["PROVISION_MQTT_PASSWORD"]))

try:
    connection = serial.Serial(os.environ["PROVISION_PORT"], 115200, timeout=0.2,
                               dsrdtr=False, rtscts=False)
except serial.SerialException as error:
    sys.exit(f"error: cannot open serial port: {error}")

try:
    connection.dtr = False
    connection.rts = False
    # Opening USB Serial/JTAG can reset CoreS3-SE. Do not transmit configuration
    # until the new console has completed its boot and printed a prompt.
    time.sleep(3.0)
    connection.reset_input_buffer()
    connection.write(b"\r\n")
    connection.flush()

    def read_until_prompt(timeout_s):
        output = bytearray()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            output.extend(connection.read(256))
            if b"esp>" in output:
                return output
        raise TimeoutError("ESP console prompt was not received")

    try:
        read_until_prompt(8.0)
    except TimeoutError as error:
        sys.exit(f"error: {error}; wait for the gateway boot to complete and retry")

    all_output = bytearray()

    def issue(command):
        connection.reset_input_buffer()
        connection.write(f"{command}\r\n".encode("utf-8"))
        connection.flush()
        response = read_until_prompt(3.0)
        all_output.extend(response)
        text = response.decode("utf-8", errors="replace")
        failures = ("Unrecognized command", "Command returned non-zero", "value too long",
                    "usage:", "commit failed")
        if any(marker in text for marker in failures):
            raise RuntimeError(f"gateway rejected command: {command}")

    for key, value in commands:
        issue(f"cfg set {key} {value}")
    issue("cfg commit")
    issue("cfg show")

    text = all_output.decode("utf-8", errors="replace").replace(password, "<hidden>")
    if text:
        print(text, end="" if text.endswith("\n") else "\n")
except (RuntimeError, TimeoutError) as error:
    sys.exit(f"error: {error}; no further provisioning commands were sent")
finally:
    connection.close()

print("Provisioning completed. The cfg show output above is the configuration verification result.")
PY

unset GATEWAY_WIFI_PASSWORD PROVISION_WIFI_PASSWORD MQTT_PASSWORD PROVISION_MQTT_PASSWORD
