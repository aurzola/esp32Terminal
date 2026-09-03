#ifndef TERM_STATUS_H
#define TERM_STATUS_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"

// Cell value marking "this column is not an icon" in the parallel icon array
// filled by StatusBar::compose.
#define STATUS_ICON_NONE 0xffu

// Bar icons (ticket 10): tokens produced by the composition and consumed by
// the renderer, which maps them to bitmap glyphs via statusIconBits.
enum StatusIcon : uint8_t {
    STATUS_ICON_WIFI = 0,
    STATUS_ICON_PLUG,
    STATUS_ICON_LOCK,
    STATUS_ICON_CHECK,
    STATUS_ICON_RETRY,
    STATUS_ICON_OFF,
    STATUS_ICON_KBD,
    STATUS_ICON_KBD_LOST,
    STATUS_ICON_COUNT,
};

// 5px-wide bar glyph bitmaps (8 rows each, MSB of each byte = leftmost
// column), indexed by StatusIcon. Lives in status.cpp so the renderer can
// build its glyph cells with layoutGlyphBits.
extern const uint8_t statusIconBits[STATUS_ICON_COUNT][8];

// Pure composition of the bottom status bar (ticket 10 / ADR-0005). The bar
// is the fixed bottom grid row (TERM_ROWS-1) painted by the app in inverse.
// This class only decides WHAT the row shows: segment layout, elision and
// transient priority (Q11). No hardware, no grid, no renderer: host-testable.
class StatusBar {
public:
    struct Inputs {
        int state;              // SshState (ssh.h); only read when sshActive
        bool sshActive;         // an SSH session exists (vs fake session)
        bool authWait;          // SSH waiting for the password on the CRT
        const char *passwordMask; // masked password when authWait
        bool pairingActive;     // pairing pin must stay on top (Q11)
        unsigned long pairingPin; // 0 = "confirm on kbd"
        const char *usbDebug;   // USB host debug line, or nullptr
        bool kbdPresent;        // a keyboard source is configured
        bool kbdConnected;
        bool showTime;          // render the time segment (SSH build)
        bool hasTime;           // NTP synced
        uint8_t day;
        uint8_t month;          // 1..12
        uint8_t hour;
        uint8_t minute;
    };

    // Fill `text` and `icon` (parallel, `cap` cells each) with the composed
    // bar row. text[c] holds an ASCII printable for text cells; icon[c] holds
    // a StatusIcon token or STATUS_ICON_NONE. Returns the number of cells
    // written (== min(cap, TERM_COLS)): the full row, spaces + NONE icons
    // where the bar has no content, so the renderer paints a solid inverse
    // strip. A cap smaller than TERM_COLS is valid and exercises the elision.
    static size_t compose(const Inputs &in, char *text, uint8_t *icon,
                          size_t cap);
};

#endif