#ifndef TERM_CONFIG_H
#define TERM_CONFIG_H

// Video geometry (CRT composite, B/N). Fixed by the NTSC LCD_CAM driver.
#define TERM_XRES 320
#define TERM_YRES 240

// Char-cell grid. Cell pitch 5x8 px: the 5x7 bitmap font is collapsed 5->4 px
// (collapseColumns) into the left 4 px of the cell, leaving a 1 px right
// gutter so adjacent glyphs don't fuse on the CRT phosphor; DEC line-drawing
// spans the full 5 px so boxes connect across cells. 56 columns fit the safe
// area exactly (280 px, same margins as the old 35x8 layout). Rows stay 8 px
// tall. Safe area: black border absorbs CRT overscan. The tube clips ~18px at
// the top and ~16px at the sides, so asymmetric margins keep every glyph
// visible: 20px top / 4px bottom. Only a re-measurement of the tube could
// unlock more columns.
#define TERM_COLS 56
#define TERM_ROWS 27
#define TERM_CELL_W 5
#define TERM_CELL_H 8
#define TERM_MARGIN_X ((TERM_XRES - TERM_COLS * TERM_CELL_W) / 2)
#define TERM_MARGIN_TOP 20
#define TERM_MARGIN_BOTTOM ((TERM_YRES - TERM_ROWS * TERM_CELL_H) - TERM_MARGIN_TOP)

// Scrollback: fixed ring of lines in PSRAM, cleared on session start/loss.
#define TERM_SCROLLBACK_LINES 4000

// Text pixel levels (8-bit; the driver LUT maps 0..255 to the CRT ramp):
// normal text is a high gray, bold reaches the top of the ramp, inverse
// swaps to a light background with dark text.
#define TERM_COLOR_TEXT 190
#define TERM_COLOR_TEXT_BOLD 255
// Dim text (SGR 2 / bright-black, fish's autosuggestion): a clearly lower
// gray than normal so suggested text reads as fainter, not duplicated.
#define TERM_COLOR_TEXT_DIM 90
#define TERM_COLOR_INV_TEXT 0
#define TERM_COLOR_INV_BG 255
#define TERM_COLOR_BG 0

// Phosphor persistence (ticket 06): 1 = each CRT field the previous content
// fades by TERM_PHOSPHOR_DECAY (0..256; 256 = no decay, 0 = instant fade),
// leaving a P39-style ghost on scroll. 0 = pixel-perfect.
#define TERM_PHOSPHOR 1
#define TERM_PHOSPHOR_DECAY 224

// Local SSH password prompt (ticket 05): max chars typed on the keyboard.
#define TERM_PASSWD_MAX 64

// Reconnect backoff for the SSH session: exponential from BASE to MAX.
#define TERM_SSH_BACKOFF_BASE_MS 1000
#define TERM_SSH_BACKOFF_MAX_MS 30000

// M1 fake session: 1 = the terminal app is up (grid, keyboard, render). When
// TERM_SSH_SESSION=0 its feed is USB-serial (fake session); see below.
#define TERM_FAKE_SESSION 1
#define TERM_LVGL_TEST 0

// SSH real session (ticket 05): 1 = the terminal is a full-screen SSH client
// over WiFi (replaces the USB-serial fake feed). Requires TERM_FAKE_SESSION=1
// (the terminal app). 0 = fake session.
#define TERM_SSH_SESSION 1

// WiFi / SSH host, from config.h (ADR-0001: fixed personal device). The SSH
// password is typed on the keyboard each connection (ADR-0002), so it is not
// stored here. These are placeholders: set your own network/host before use.
#define TERM_WIFI_SSID ""
#define TERM_WIFI_PASS ""
#define TERM_SSH_HOST ""
#define TERM_SSH_PORT 22
#define TERM_SSH_USER ""
// SSH password for auto-login (ticket 05). Empty = ADR-0002 default (typed on
// the CRT each connection). Non-empty = sent automatically on connect (no CRT
// prompt), falling back to the prompt only if the host rejects it.
#define TERM_SSH_PWD ""

// NTP timezone (POSIX TZ string) for the status bar clock (ticket 10), applied
// with configTzTime when the SSH task connects WiFi. America/Bogota (UTC-5, no
// DST): matches the dev machine and the SSH host.
#define TERM_TZ "COT5"

// WiFi connect timeout (ms) and libssh log verbosity (0 = quiet).
#define TERM_SSH_WIFI_TIMEOUT_MS 20000
#define TERM_SSH_VERBOSITY 0
// libssh session timeout (s): bounds the blocking pty/shell channel requests
// so a lost server reply (radio contention) times out and reconnects instead
// of blocking the SSH task forever (SSH_TIMEOUT_INFINITE).
#define TERM_SSH_TIMEOUT_SEC 10

// libssh needs a large task stack, and the task touches flash-backed WiFi
// (connect reads calibration/NVS with the cache disabled), so the stack MUST
// live in internal DRAM. The DMA field buffer lives in PSRAM, leaving this
// much internal DRAM free at boot. High-water mark is logged at boot so it
// can be tuned.
#define TERM_SSH_STACK_BYTES 32768

// BLE keyboard host (ticket 04): the fixed HID keyboard this terminal
// connects to, as a "AA:BB:CC:DD:EE:FF" address string. Empty = disabled
// (the default build just relies on USB-serial).
#define TERM_KBD_MAC ""

// Auto-discovery prefix: some keyboards rotate the low bytes of a fixed-MAC
// prefix and stop sending a readable name from the ESP's perspective. Any
// advertiser whose address starts with this string is treated as the keyboard.
// Empty = name filter only.
#define TERM_KBD_PREFIX "13:05:aa"

// Some budget BLE keyboards advertise with a non-resolvable private address
// that rotates every few seconds, so a fixed MAC can never connect (the peer
// is always seen under a new address). TERM_KBD_AUTO=1 scans on every
// (re)connect, picks the advertised keyboard by name and connects to the live
// address. 0 = connect to the fixed TERM_KBD_MAC.
#define TERM_KBD_AUTO 1

// BLE discovery (ticket 04): 1 = scan all BLE devices at boot and log their
// address/type/name on serial ([kbd-scan]) so the real keyboard MAC can be
// copied into TERM_KBD_MAC. 0 = normal keyboard connection.
#define TERM_KBD_SCAN 0
#define TERM_KBD_SCAN_SEC 12

// Fixed passkey shown on the status line for the first pairing. Kept stable
// across retries so it can be typed on the keyboard without the code changing.
#define TERM_KBD_PIN 123456

// Keyboard input source (ticket 04): 0 = BLE HID host (TERM_KBD_MAC), 1 = USB
// keyboard on the native USB-OTG port (USB Host HID boot protocol). The
// KeyMap seam is shared; only the transport differs.
#define TERM_KBD_USB 0

// Physical keyboard layout used to translate HID keycodes into terminal bytes
// (KeyMap::Layout). 0 = US ANSI (default), 1 = Spanish ISO with AltGr as the
// right Alt, where AltGr gives the shell symbols ( [ ] { } \ | @ # ~ ... ).
#define TERM_KBD_LAYOUT 1

// Diagnostics: set to 1 to skip starting the NTSC video driver and not block
// the loop on frame timing. Used to isolate BLE radio contention against the
// GDMA video path during the ticket-04 GATT debugging.
#define TERM_TEST_NOVIDEO 0

// Key-click audio (same scheme as esp32LanderS3/src/audio.cpp): a short
// synthesized click on every key press, played as LEDC PWM on TERM_CLICK_GPIO
// driven by a 16 kHz timer ISR. 1 = enabled.
#define TERM_CLICK 1
#define TERM_CLICK_GPIO 18

#endif
