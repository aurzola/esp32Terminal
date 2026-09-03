#!/bin/bash
# Compile esp32Terminal for the ESP32-S3 (CRT composite via LCD_CAM+GDMA).
# Runs the host unit tests (tests/term) first; skip with --skip-host-tests.
set -u
DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ "${1:-}" != "--skip-host-tests" ]]; then
    g++ -std=c++17 -I "$DIR/src" -o /tmp/term_test \
        "$DIR/tests/term/term_test.cpp" "$DIR/src/term.cpp" &&
    /tmp/term_test && \
    g++ -std=c++17 -I "$DIR/src" -o /tmp/keymap_test \
        "$DIR/tests/keymap/keymap_test.cpp" "$DIR/src/keymap.cpp" &&
    /tmp/keymap_test && \
    g++ -std=c++17 -I "$DIR/src" -o /tmp/phosphor_test \
        "$DIR/tests/phosphor/phosphor_test.cpp" &&
    /tmp/phosphor_test && \
    g++ -std=c++17 -I "$DIR/src" -o /tmp/passprompt_test \
        "$DIR/tests/passprompt/passprompt_test.cpp" "$DIR/src/passprompt.cpp" &&
    /tmp/passprompt_test && \
    g++ -std=c++17 -I "$DIR/src" -o /tmp/status_test \
        "$DIR/tests/status/status_test.cpp" "$DIR/src/status.cpp" &&
    /tmp/status_test || {
        echo "host tests failed, aborting build" >&2
        exit 1
    }
fi

# NimBLE-Arduino works as a HID host against a single fixed keyboard; keep its
# memory footprint out of internal DRAM (the CRT video framebuffer already
# lives there) by placing the BLE pools in PSRAM and trimming the counts.
NIMBLE_DEFS="-DCONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=1 \
-DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1 \
-DCONFIG_BT_NIMBLE_MAX_BONDS=1 \
-DCONFIG_BT_NIMBLE_MAX_CCCDS=4 \
-DCONFIG_BT_NIMBLE_ROLE_BROADCASTER=0 \
-DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL=0"

# LibSSH-ESP32: force the native lwIP poll() instead of its bsd_poll
# (select-based) emulation. bsd_poll does a recv(MSG_PEEK) on the socket
# while a non-blocking connect is in progress, gets EINPROGRESS, classifies
# it as POLLERR and aborts the connect with ENOTCONN. lwIP provides a real
# poll(), so HAVE_POLL makes the non-blocking connect complete (SSH connects
# in ~600ms instead of failing in ~120ms).
SSH_DEFS="-DHAVE_POLL=1"

arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app \
    --build-property compiler.c.extra_flags="-I$DIR/src $NIMBLE_DEFS $SSH_DEFS" \
    --build-property compiler.cpp.extra_flags="-I$DIR/src $NIMBLE_DEFS $SSH_DEFS" \
    --build-property compiler.S.extra_flags="-I$DIR/src $NIMBLE_DEFS $SSH_DEFS" \
    --build-path "$DIR/build" \
    "$DIR/esp32Terminal.ino" "$@"