#!/bin/bash
# Upload esp32Terminal to the CRT board (/dev/ttyACM0).
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
arduino-cli upload --fqbn esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app \
    --port /dev/ttyACM0 \
    --input-dir "$DIR/build" \
    "$DIR/esp32Terminal.ino"