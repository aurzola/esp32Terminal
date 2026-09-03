// Host unit tests for StatusBar (ticket 10 / ADR-0005). No Arduino, no LVGL,
// no hardware: only the pure composition seam. Compile on the PC:
//   g++ -std=c++17 -I ../../src -o /tmp/status_test status_test.cpp ../../src/status.cpp

#include <cstdio>
#include <cstring>
#include <string>

#include "status.h"
#include "ssh.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

// Render the composed row into a readable string: '#' = icon cell, otherwise
// the ASCII text.
static std::string rowText(const char *text, const uint8_t *icon, int n)
{
    std::string s;
    for (int i = 0; i < n; i++) {
        s += icon[i] == STATUS_ICON_NONE ? text[i] : '#';
    }
    return s;
}

// A left-aligned message padded with spaces to the full row width.
static std::string padLeft(const char *msg)
{
    return std::string(msg) + std::string(TERM_COLS - strlen(msg), ' ');
}

int main(void)
{
    // ---- normal bar: ssh connected + kbd + time, right-aligned (56 cols)
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.state = SSH_STATE_SHELL;
        in.sshActive = true;
        in.kbdPresent = true;
        in.kbdConnected = true;
        in.showTime = true;
        in.hasTime = true;
        in.day = 31;
        in.month = 8;
        in.hour = 14;
        in.minute = 50;

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        size_t n = StatusBar::compose(in, text, icon, sizeof(text));
        CHECK(n == TERM_COLS);
        // "#conect #kbd " (13) + 31 spaces + "31-AUG 14:50" (12)
        std::string expected = std::string("#conect #kbd ") +
                               std::string(TERM_COLS - 13 - 12, ' ') +
                               "31-AUG 14:50";
        CHECK(expected.size() == TERM_COLS);
        CHECK(rowText(text, icon, TERM_COLS) == expected);
        CHECK(icon[0] == STATUS_ICON_CHECK);
        CHECK(icon[8] == STATUS_ICON_KBD);
    }

    // ---- connecting + kbd lost: plug icon, cross icon, "kbd lost"
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.sshActive = true;
        in.state = SSH_STATE_CONNECT;
        in.kbdPresent = true;
        in.kbdConnected = false;

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        StatusBar::compose(in, text, icon, sizeof(text));
        // "#connect. #kbd lost " (20) + 36 spaces
        std::string expected = std::string("#connect. #kbd lost ") +
                               std::string(TERM_COLS - 20, ' ');
        CHECK(expected.size() == TERM_COLS);
        CHECK(rowText(text, icon, TERM_COLS) == expected);
        CHECK(icon[0] == STATUS_ICON_PLUG);
        CHECK(icon[10] == STATUS_ICON_KBD_LOST);
    }

    // ---- no ssh session (fake build): only the kbd segment, no time
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.sshActive = false;
        in.kbdPresent = true;
        in.kbdConnected = true;
        in.showTime = false;

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        StatusBar::compose(in, text, icon, sizeof(text));
        std::string expected = std::string("#kbd ") +
                               std::string(TERM_COLS - 5, ' ');
        CHECK(rowText(text, icon, TERM_COLS) == expected);
        CHECK(icon[0] == STATUS_ICON_KBD);
    }

    // ---- time not synced: "--:--" right-aligned
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.sshActive = true;
        in.state = SSH_STATE_SHELL;
        in.kbdPresent = true;
        in.kbdConnected = true;
        in.showTime = true;
        in.hasTime = false;

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        StatusBar::compose(in, text, icon, sizeof(text));
        CHECK(rowText(text, icon, TERM_COLS).substr(TERM_COLS - 5, 5) == "--:--");
    }

    // ---- authWait hijacks the bar with the masked password; kbd lost suffix
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.sshActive = true;
        in.authWait = true;
        in.passwordMask = "****";
        in.kbdPresent = true;
        in.kbdConnected = false;

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        StatusBar::compose(in, text, icon, sizeof(text));
        std::string expected = padLeft("ssh password: **** | kbd lost");
        CHECK(rowText(text, icon, TERM_COLS) == expected);
        CHECK(icon[0] == STATUS_ICON_NONE);
    }

    // ---- authWait without kbd lost suffix when the keyboard is connected
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.sshActive = true;
        in.authWait = true;
        in.passwordMask = "****";
        in.kbdPresent = true;
        in.kbdConnected = true;

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        StatusBar::compose(in, text, icon, sizeof(text));
        std::string expected = padLeft("ssh password: ****");
        CHECK(rowText(text, icon, TERM_COLS) == expected);
    }

    // ---- pairing outranks the password prompt (Q11)
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.sshActive = true;
        in.authWait = true;
        in.passwordMask = "**";
        in.pairingActive = true;
        in.pairingPin = 123456;

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        StatusBar::compose(in, text, icon, sizeof(text));
        std::string expected = padLeft("pairing: type 123456");
        CHECK(rowText(text, icon, TERM_COLS) == expected);
    }

    // ---- pairing "confirm on kbd" (pin 0)
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.pairingActive = true;
        in.pairingPin = 0;

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        StatusBar::compose(in, text, icon, sizeof(text));
        std::string expected = padLeft("pairing: confirm on kbd");
        CHECK(rowText(text, icon, TERM_COLS) == expected);
    }

    // ---- usb debug shows only when no action required (Q11)
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.usbDebug = "usb host: enumerating...";

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        StatusBar::compose(in, text, icon, sizeof(text));
        std::string expected = padLeft("usb host: enumerating...");
        CHECK(rowText(text, icon, TERM_COLS) == expected);

        in.pairingActive = true; // pairing wins over usb debug
        in.pairingPin = 999999;
        StatusBar::compose(in, text, icon, sizeof(text));
        CHECK(rowText(text, icon, TERM_COLS) == padLeft("pairing: type 999999"));
    }

    // ---- long usb debug line truncates to the row width
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.usbDebug = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
                      "XXXXXXXXXXXXXXXXXXXXXXXX";

        char text[TERM_COLS];
        uint8_t icon[TERM_COLS];
        StatusBar::compose(in, text, icon, sizeof(text));
        CHECK(rowText(text, icon, TERM_COLS) == std::string(TERM_COLS, 'X'));
    }

    // ---- elision: small cap forces levels down to short time / no time
    {
        StatusBar::Inputs in;
        memset(&in, 0, sizeof(in));
        in.state = SSH_STATE_SHELL;
        in.sshActive = true;
        in.kbdPresent = true;
        in.kbdConnected = true;
        in.showTime = true;
        in.hasTime = true;
        in.hour = 14;
        in.minute = 50;

        // cap 8: ssh icon only + "14:50" right-aligned
        {
            char text[8];
            uint8_t icon[8];
            StatusBar::compose(in, text, icon, sizeof(text));
            CHECK(icon[0] == STATUS_ICON_CHECK);
            CHECK(rowText(text, icon, 8) == std::string("#  ") + "14:50");
        }
        // cap 2: ssh icon only, no time
        {
            char text[2];
            uint8_t icon[2];
            StatusBar::compose(in, text, icon, sizeof(text));
            CHECK(icon[0] == STATUS_ICON_CHECK);
            CHECK(text[1] == ' ');
        }
    }

    if (g_failures == 0) {
        printf("status: all tests passed\n");
        return 0;
    }
    printf("status: %d FAILURES\n", g_failures);
    return 1;
}