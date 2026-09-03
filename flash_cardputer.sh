#!/bin/bash
# Upload the Cardputer BLE keyboard app. The Cardputer enumerates as its own
# USB serial device when connected via USB-C; pass the port as $1 or it probes.
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-}"
if [[ -z "$PORT" ]]; then
    PORT="$(arduino-cli board list 2>/dev/null | grep -iE "ttyUSB|ttyACM" | head -1 | awk '{print $1}')"
fi
if [[ -z "$PORT" ]]; then
    echo "no serial port found for the Cardputer" >&2
    exit 1
fi
arduino-cli upload --fqbn "esp32:esp32:m5stack_cardputer:CDCOnBoot=cdc,USBMode=hwcdc" \
    --port "$PORT" \
    --input-dir "$DIR/build_cardputer" \
    "$DIR/cardputer/cardputer_kbd/cardputer_kbd.ino"
