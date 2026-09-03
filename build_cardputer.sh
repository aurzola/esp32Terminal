#!/bin/bash
# Compile the Cardputer BLE keyboard app (peripheral for the terminal's BLE
# HID host).
#
# Uses the esp32:esp32 core (same as the terminal) so both ends share the same
# SDK / NimBLE version; the m5stack:esp32 core has an older NimBLE that failed
# the GATT service discovery (peer saw 0 services).
#
# Build flags:
# - CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE: the NimBLE host task needs a larger
#   stack for the HID GATT database.
# - CONFIG_ESP_IPC_TASK_STACK_SIZE: avoid IPC task overflows while the BLE host
#   allocates its interrupt at startup.
# - CONFIG_ESP_MAIN_TASK_STACK_SIZE: the Arduino main/loop task runs
#   M5Cardputer.begin + NimBLEDevice::init + M5GFX in setup.
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
NIMBLE_DEFS="-DCONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=8192 \
-DCONFIG_ESP_IPC_TASK_STACK_SIZE=2048 \
-DCONFIG_ESP_MAIN_TASK_STACK_SIZE=8192"
arduino-cli compile --fqbn "esp32:esp32:m5stack_cardputer:CDCOnBoot=cdc,USBMode=hwcdc" \
    --build-property compiler.c.extra_flags="$NIMBLE_DEFS" \
    --build-property compiler.cpp.extra_flags="$NIMBLE_DEFS" \
    --build-path "$DIR/build_cardputer" \
    "$DIR/cardputer/cardputer_kbd/cardputer_kbd.ino" "$@"
