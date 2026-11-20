#!/usr/bin/env bash
# Builds the firmware, writes the factory and OTA images into docs/firmware/
# and regenerates the web flasher site. Needs PlatformIO and esptool (v4 or v5).
set -euo pipefail
cd "$(dirname "$0")/.."

VER=$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' src/main.c)
[ -n "$VER" ] || { echo "FW_VERSION not found in src/main.c" >&2; exit 1; }

ESPTOOL=${ESPTOOL:-$(command -v esptool || command -v esptool.py || true)}
[ -n "$ESPTOOL" ] || { echo "esptool not found: pip install esptool" >&2; exit 1; }
MERGE=merge-bin
"$ESPTOOL" version 2>/dev/null | grep -q 'v4\.' && MERGE=merge_bin

pio run
B=.pio/build/esp32s3
mkdir -p docs/firmware
rm -f docs/firmware/*.bin

"$ESPTOOL" --chip esp32s3 "$MERGE" -o "docs/firmware/grbl-wifi-bridge-esp32s3-v$VER-factory.bin" --flash-size 8MB \
  0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xf000 "$B/ota_data_initial.bin" 0x20000 "$B/firmware.bin"
cp "$B/firmware.bin" "docs/firmware/grbl-wifi-bridge-esp32s3-v$VER-ota.bin"

python3 tools/build_site.py
ls -l docs/firmware
