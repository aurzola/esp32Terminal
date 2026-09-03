#include "status.h"

#include <stdio.h>
#include <string.h>

#include "ssh.h"

namespace {

// Short labels for the SSH state (ticket 10), indexed by SshState. Pure ASCII
// (the grid only renders 0x20..0x7e): the trailing dot reads as "in progress"
// instead of a non-ASCII ellipsis.
const char *const kSshLabel[7] = {
    "off", "wifi.", "connect.", "auth.", "auth.", "conect", "retry.",
};

const uint8_t kSshIcon[7] = {
    STATUS_ICON_OFF, STATUS_ICON_WIFI, STATUS_ICON_PLUG, STATUS_ICON_LOCK,
    STATUS_ICON_LOCK, STATUS_ICON_CHECK, STATUS_ICON_RETRY,
};

const char *const kMonth[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

// Copy a plain ASCII message (transient text) into the row, truncating to
// maxCols. All cells are text, no icons.
size_t copyPlain(const char *msg, char *text, uint8_t *icon, size_t maxCols)
{
    for (size_t i = 0; i < maxCols; i++) {
        icon[i] = STATUS_ICON_NONE;
        text[i] = ' ';
    }
    for (size_t s = 0; msg[s] != '\0' && s < maxCols; s++) {
        text[s] = msg[s];
    }
    return maxCols;
}

// Format the time segment into tbuf. Full = "DD-MON HH:MM", short = "HH:MM".
size_t fmtTime(char *tbuf, size_t cap, const StatusBar::Inputs &in, bool short_)
{
    if (in.hasTime && !short_) {
        const char *mon = (in.month >= 1 && in.month <= 12) ? kMonth[in.month - 1]
                                                           : "???";
        return (size_t)snprintf(tbuf, cap, "%02u-%s %02u:%02u",
                                (unsigned)in.day, mon, (unsigned)in.hour,
                                (unsigned)in.minute);
    }
    if (in.hasTime) {
        return (size_t)snprintf(tbuf, cap, "%02u:%02u", (unsigned)in.hour,
                                (unsigned)in.minute);
    }
    return (size_t)snprintf(tbuf, cap, "--:--");
}

} // namespace

// Bar glyph bitmaps: 5 px wide, 8 rows, MSB of each row byte = leftmost column.
const uint8_t statusIconBits[STATUS_ICON_COUNT][8] = {
    // STATUS_ICON_WIFI: antenna with signal arcs
    {0x08, 0x08, 0x28, 0x88, 0x28, 0x08, 0x00, 0x00},
    // STATUS_ICON_PLUG: plug pin over a bar
    {0x00, 0x08, 0x38, 0x38, 0x38, 0x08, 0x08, 0x00},
    // STATUS_ICON_LOCK: shackle + body
    {0x00, 0x38, 0x88, 0x88, 0xf8, 0x88, 0x88, 0xf8},
    // STATUS_ICON_CHECK: tick
    {0x00, 0x08, 0x10, 0x20, 0x48, 0x80, 0x00, 0x00},
    // STATUS_ICON_RETRY: clock face
    {0x00, 0x38, 0x88, 0xa8, 0xa8, 0x88, 0x38, 0x00},
    // STATUS_ICON_OFF: dash
    {0x00, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00},
    // STATUS_ICON_KBD: keyboard
    {0x00, 0x00, 0xf8, 0xa8, 0xa8, 0xf8, 0x38, 0x00},
    // STATUS_ICON_KBD_LOST: cross
    {0x88, 0x28, 0x08, 0x08, 0x08, 0x28, 0x88, 0x00},
};

size_t StatusBar::compose(const Inputs &in, char *text, uint8_t *icon,
                          size_t cap)
{
    const size_t maxCols = cap < TERM_COLS ? cap : TERM_COLS;

    // Transient priority (Q11): pairing > password > usb debug > normal bar.
    // Pairing and the password prompt hijack the whole row until resolved; the
    // USB debug line is one-shot and only shows when no action is required.
    if (in.pairingActive) {
        char msg[24];
        if (in.pairingPin == 0) {
            snprintf(msg, sizeof(msg), "pairing: confirm on kbd");
        } else {
            snprintf(msg, sizeof(msg), "pairing: type %lu",
                     (unsigned long)in.pairingPin);
        }
        return copyPlain(msg, text, icon, maxCols);
    }
    if (in.authWait) {
        char msg[48];
        int n = snprintf(msg, sizeof(msg), "ssh password: %s",
                         in.passwordMask != nullptr ? in.passwordMask : "");
        if (!in.kbdConnected && n > 0 && (size_t)n < sizeof(msg)) {
            snprintf(msg + n, sizeof(msg) - (size_t)n, " | kbd lost");
        }
        return copyPlain(msg, text, icon, maxCols);
    }
    if (in.usbDebug != nullptr && in.usbDebug[0] != '\0') {
        return copyPlain(in.usbDebug, text, icon, maxCols);
    }

    // Normal bar: [ssh icon+label] [kbd icon+label] right-aligned time.
    // Elision levels (never triggered at 56 cols with the short labels, but
    // kept deterministic and exercised by the host tests with a small cap):
    // the ssh icon is the most important cell and never elides:
    //   0 full · 1 no ssh label · 2 no kbd label · 3 no kbd · 4 short time ·
    //   5 no time.
    int st = in.state;
    if (st < 0 || st > 6) {
        st = 0;
    }
    const char *sshLabel = in.sshActive ? kSshLabel[st] : nullptr;
    const uint8_t sshIc = in.sshActive ? kSshIcon[st] : STATUS_ICON_NONE;
    const char *kbdLabel = in.kbdConnected ? "kbd" : "kbd lost";
    const uint8_t kbdIc = in.kbdConnected ? STATUS_ICON_KBD
                                          : STATUS_ICON_KBD_LOST;

    for (int level = 0; level <= 5; level++) {
        for (size_t i = 0; i < maxCols; i++) {
            icon[i] = STATUS_ICON_NONE;
            text[i] = ' ';
        }
        size_t n = 0;

        if (in.sshActive) {
            if (n < maxCols) {
                icon[n] = sshIc; // icon cell; text stays a blank
                n++;
            }
            if (level == 0 && sshLabel != nullptr) {
                for (const char *p = sshLabel; *p != '\0' && n < maxCols; p++) {
                    text[n++] = *p;
                }
            }
            if (n < maxCols) {
                text[n++] = ' '; // separator
            }
        }
        if (in.kbdPresent) {
            if (level <= 2 && n < maxCols) {
                icon[n] = kbdIc;
                n++;
            }
            if (level <= 1) {
                for (const char *p = kbdLabel; *p != '\0' && n < maxCols; p++) {
                    text[n++] = *p;
                }
            }
            if (n < maxCols) {
                text[n++] = ' '; // separator
            }
        }

        char tbuf[16];
        size_t tlen = 0;
        if (in.showTime) {
            if (level <= 3) {
                tlen = fmtTime(tbuf, sizeof(tbuf), in, false);
            } else if (level == 4) {
                tlen = fmtTime(tbuf, sizeof(tbuf), in, true);
            }
        }

        if (n + tlen <= maxCols) {
            size_t start = maxCols - tlen;
            for (size_t i = 0; i < tlen; i++) {
                text[start + i] = tbuf[i];
            }
            return maxCols;
        }
    }
    return maxCols;
}