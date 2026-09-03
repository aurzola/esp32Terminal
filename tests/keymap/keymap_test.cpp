// Host unit tests for KeyMap (USB HID keycode + modifiers -> terminal bytes).
// Seam: the public KeyMap::map function. Compile on the PC:
//   g++ -std=c++17 -I ../../src -o /tmp/keymap_test keymap_test.cpp ../../src/keymap.cpp

#include <cstdio>
#include <cstring>

#include "keymap.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static void checkMapL(uint8_t keycode, uint8_t mods, KeyMap::Layout layout,
                      const char *want, int n, const char *label)
{
    char out[16];
    int len = KeyMap::map(keycode, mods, out, sizeof(out), layout);
    if (len != n || memcmp(out, want, (size_t)n) != 0) {
        printf("FAIL %s: key %d mods %02x -> len %d want %d\n", label,
               keycode, mods, len, n);
        g_failures++;
    }
}

// Map a key (US layout by default) and compare the produced bytes to `want`.
static void checkMap(uint8_t keycode, uint8_t mods, const char *want, int n,
                     const char *label)
{
    checkMapL(keycode, mods, KeyMap::LAYOUT_US, want, n, label);
}

int main(void)
{
    // ---- letters: keycodes 0x04..0x1d map to a..z (US ANSI)
    {
        checkMap(0x04 + ('A' - 'A'), 0, "a", 1, "letter-a");
        checkMap(0x04 + ('m' - 'a'), 0, "m", 1, "letter-m");
        checkMap(0x04 + ('z' - 'a'), 0, "z", 1, "letter-z");
        checkMap(0x04 + ('A' - 'A'), KeyMap::MOD_LSHIFT, "A", 1, "letter-A");
        checkMap(0x04 + ('z' - 'a'), KeyMap::MOD_RSHIFT, "Z", 1, "letter-Z");
    }

    // ---- digits: keycodes 0x1e..0x27 are 1..9,0; shift gives the symbols
    {
        checkMap(0x1e, 0, "1", 1, "digit-1");
        checkMap(0x27, 0, "0", 1, "digit-0");
        checkMap(0x1e, KeyMap::MOD_LSHIFT, "!", 1, "shift-1");
        checkMap(0x1f, KeyMap::MOD_LSHIFT, "@", 1, "shift-2");
        checkMap(0x20, KeyMap::MOD_LSHIFT, "#", 1, "shift-3");
        checkMap(0x21, KeyMap::MOD_LSHIFT, "$", 1, "shift-4");
        checkMap(0x22, KeyMap::MOD_LSHIFT, "%", 1, "shift-5");
        checkMap(0x23, KeyMap::MOD_LSHIFT, "^", 1, "shift-6");
        checkMap(0x24, KeyMap::MOD_LSHIFT, "&", 1, "shift-7");
        checkMap(0x25, KeyMap::MOD_LSHIFT, "*", 1, "shift-8");
        checkMap(0x26, KeyMap::MOD_LSHIFT, "(", 1, "shift-9");
        checkMap(0x27, KeyMap::MOD_LSHIFT, ")", 1, "shift-0");
    }

    // ---- punctuation keys and their shifted forms (US ANSI)
    {
        // 0x2d -/_, 0x2e =/+, 0x2f [/{, 0x30 ]/}, 0x31 \\/|
        checkMap(0x2d, 0, "-", 1, "minus");
        checkMap(0x2d, KeyMap::MOD_LSHIFT, "_", 1, "underscore");
        checkMap(0x2e, 0, "=", 1, "equals");
        checkMap(0x2e, KeyMap::MOD_LSHIFT, "+", 1, "plus");
        checkMap(0x2f, 0, "[", 1, "lbracket");
        checkMap(0x2f, KeyMap::MOD_LSHIFT, "{", 1, "lbrace");
        checkMap(0x30, 0, "]", 1, "rbracket");
        checkMap(0x30, KeyMap::MOD_LSHIFT, "}", 1, "rbrace");
        checkMap(0x31, 0, "\\", 1, "backslash");
        checkMap(0x31, KeyMap::MOD_LSHIFT, "|", 1, "pipe");
        // 0x33 ;/:, 0x34 '/", 0x35 `/~, 0x36 ,/<, 0x37 ./>, 0x38 //?
        checkMap(0x33, 0, ";", 1, "semicolon");
        checkMap(0x33, KeyMap::MOD_LSHIFT, ":", 1, "colon");
        checkMap(0x34, 0, "'", 1, "apostrophe");
        checkMap(0x34, KeyMap::MOD_LSHIFT, "\"", 1, "quote");
        checkMap(0x35, 0, "`", 1, "grave");
        checkMap(0x35, KeyMap::MOD_LSHIFT, "~", 1, "tilde");
        checkMap(0x36, 0, ",", 1, "comma");
        checkMap(0x36, KeyMap::MOD_LSHIFT, "<", 1, "less");
        checkMap(0x37, 0, ".", 1, "period");
        checkMap(0x37, KeyMap::MOD_LSHIFT, ">", 1, "greater");
        checkMap(0x38, 0, "/", 1, "slash");
        checkMap(0x38, KeyMap::MOD_LSHIFT, "?", 1, "question");
    }

    // ---- basic editing keys
    {
        checkMap(0x2c, 0, " ", 1, "space");            // Space
        checkMap(0x28, 0, "\r", 1, "enter");           // Enter -> CR
        checkMap(0x2a, 0, "\x7f", 1, "backspace");     // Backspace -> DEL
        checkMap(0x2b, 0, "\t", 1, "tab");             // Tab -> HT
        checkMap(0x29, 0, "\x1b", 1, "esc");           // Esc
    }

    // ---- navigation keys (xterm-style CSI sequences)
    {
        checkMap(0x52, 0, "\x1b[A", 3, "up");
        checkMap(0x51, 0, "\x1b[B", 3, "down");
        checkMap(0x4f, 0, "\x1b[C", 3, "right");
        checkMap(0x50, 0, "\x1b[D", 3, "left");
        checkMap(0x4a, 0, "\x1b[H", 3, "home");
        checkMap(0x4d, 0, "\x1b[F", 3, "end");
        checkMap(0x4b, 0, "\x1b[5~", 4, "pageup");
        checkMap(0x4e, 0, "\x1b[6~", 4, "pagedown");
        checkMap(0x49, 0, "\x1b[2~", 4, "insert");
        checkMap(0x4c, 0, "\x1b[3~", 4, "delete-fwd");
    }

    // ---- function keys: F1-F4 send SS3, F5-F12 send CSI ~ sequences
    {
        checkMap(0x58, 0, "\x1bOP", 3, "f1");
        checkMap(0x59, 0, "\x1bOQ", 3, "f2");
        checkMap(0x5a, 0, "\x1bOR", 3, "f3");
        checkMap(0x5b, 0, "\x1bOS", 3, "f4");
        checkMap(0x5c, 0, "\x1b[15~", 5, "f5");
        checkMap(0x5d, 0, "\x1b[17~", 5, "f6");
        checkMap(0x5e, 0, "\x1b[18~", 5, "f7");
        checkMap(0x5f, 0, "\x1b[19~", 5, "f8");
        checkMap(0x60, 0, "\x1b[20~", 5, "f9");
        checkMap(0x61, 0, "\x1b[21~", 5, "f10");
        checkMap(0x62, 0, "\x1b[23~", 5, "f11");
        checkMap(0x63, 0, "\x1b[24~", 5, "f12");
    }

    // ---- Ctrl+letter -> control char (ASCII 0x01..0x1a), Ctrl+Shift same
    {
        checkMap(0x04, KeyMap::MOD_LCTRL, "\x01", 1, "ctrl-a");
        checkMap(0x04, KeyMap::MOD_LCTRL | KeyMap::MOD_LSHIFT, "\x01", 1, "ctrl-shift-a");
        checkMap(0x08, KeyMap::MOD_RCTRL, "\x05", 1, "ctrl-e");
        checkMap(0x1d, KeyMap::MOD_LCTRL, "\x1a", 1, "ctrl-z");
        // Ctrl+[ = ESC, Ctrl+\ = FS, Ctrl+] = GS
        checkMap(0x2f, KeyMap::MOD_LCTRL, "\x1b", 1, "ctrl-lbracket");
        checkMap(0x31, KeyMap::MOD_LCTRL, "\x1c", 1, "ctrl-backslash");
        checkMap(0x30, KeyMap::MOD_LCTRL, "\x1d", 1, "ctrl-rbracket");
        // Ctrl+digit has no control-char meaning -> not mapped
        checkMap(0x1e, KeyMap::MOD_LCTRL, "", 0, "ctrl-1");
    }

    // ---- Alt+printable -> ESC + the character (Meta)
    {
        checkMap(0x04, KeyMap::MOD_LALT, "\x1b" "a", 2, "alt-a");
        checkMap(0x1e, KeyMap::MOD_LALT, "\x1b" "1", 2, "alt-1");
        checkMap(0x2c, KeyMap::MOD_LALT, "\x1b" " ", 2, "alt-space");
        // Alt+Shift+letter -> ESC + uppercase
        checkMap(0x04, KeyMap::MOD_LALT | KeyMap::MOD_LSHIFT, "\x1b" "A", 2, "alt-shift-a");
    }

    // ---- cap contract: a buffer smaller than the longest sequence (5) is
    // rejected and nothing is written
    {
        char tiny[4];
        memset(tiny, 0x5a, sizeof(tiny));
        int len = KeyMap::map(0x5c, 0, tiny, sizeof(tiny)); // F5 -> 5 bytes
        if (len != 0 || tiny[0] != 0x5a) {
            printf("FAIL cap: len %d tiny[0]=%02x\n", len, tiny[0]);
            g_failures++;
        }
        char small[5];
        len = KeyMap::map(0x04, 0, small, sizeof(small)); // 'a' fits
        if (len != 1 || small[0] != 'a') {
            printf("FAIL cap-fit: len %d\n", len);
            g_failures++;
        }
    }

    // ---- Spanish (ES) layout: same letters/space/editing/nav/fn as US, but
    // the number and punctuation rows differ and add AltGr (RALT) third level
    {
        // letters, space, and editing keys are unchanged from US
        checkMapL(0x04, 0, KeyMap::LAYOUT_ES, "a", 1, "es-letter-a");
        checkMapL(0x04, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "A", 1,
                  "es-letter-A");
        checkMapL(0x2c, 0, KeyMap::LAYOUT_ES, " ", 1, "es-space");
        checkMapL(0x28, 0, KeyMap::LAYOUT_ES, "\r", 1, "es-enter");
        checkMapL(0x52, 0, KeyMap::LAYOUT_ES, "\x1b[A", 3, "es-up");
    }

    // ---- ES number row: base / shift / AltGr third level
    {
        checkMapL(0x1e, 0, KeyMap::LAYOUT_ES, "1", 1, "es-n1");
        checkMapL(0x1e, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "!", 1,
                  "es-n1-shift");
        checkMapL(0x1f, 0, KeyMap::LAYOUT_ES, "2", 1, "es-n2");
        checkMapL(0x1f, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "\"", 1,
                  "es-n2-shift");
        checkMapL(0x1f, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "@", 1,
                  "es-n2-altgr");
        checkMapL(0x20, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "\xc2\xb7", 2,
                  "es-n3-shift");
        checkMapL(0x20, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "#", 1,
                  "es-n3-altgr");
        checkMapL(0x21, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "~", 1,
                  "es-n4-altgr");
        checkMapL(0x22, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "\xe2\x82\xac", 3,
                  "es-n5-altgr-euro");
        checkMapL(0x23, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "&", 1,
                  "es-n6-shift");
        checkMapL(0x23, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "\xc2\xac", 2,
                  "es-n6-altgr-not");
        checkMapL(0x24, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "/", 1,
                  "es-n7-shift");
        checkMapL(0x24, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "{", 1,
                  "es-n7-altgr");
        checkMapL(0x25, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "[", 1,
                  "es-n8-altgr");
        checkMapL(0x26, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "]", 1,
                  "es-n9-altgr");
        checkMapL(0x27, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "=", 1,
                  "es-n0-shift");
        checkMapL(0x27, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "}", 1,
                  "es-n0-altgr");
    }

    // ---- ES punctuation row: base / shift / AltGr
    {
        checkMapL(0x2d, 0, KeyMap::LAYOUT_ES, "'", 1, "es-p29");
        checkMapL(0x2d, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "?", 1,
                  "es-p29-shift");
        checkMapL(0x2d, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "\\", 1,
                  "es-p29-altgr");
        checkMapL(0x2e, 0, KeyMap::LAYOUT_ES, "\xc2\xa1", 2, "es-p2e");
        checkMapL(0x2e, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "\xc2\xbf", 2,
                  "es-p2e-shift");
        checkMapL(0x2f, 0, KeyMap::LAYOUT_ES, "`", 1, "es-p2f");
        checkMapL(0x2f, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "\xc2\xa8", 2,
                  "es-p2f-shift");
        checkMapL(0x2f, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "[", 1,
                  "es-p2f-altgr");
        checkMapL(0x30, 0, KeyMap::LAYOUT_ES, "+", 1, "es-p30");
        checkMapL(0x30, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "*", 1,
                  "es-p30-shift");
        checkMapL(0x30, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "]", 1,
                  "es-p30-altgr");
        checkMapL(0x31, 0, KeyMap::LAYOUT_ES, "\xc3\xa7", 2, "es-p31");
        checkMapL(0x31, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "\xc3\x87", 2,
                  "es-p31-shift");
        checkMapL(0x31, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "}", 1,
                  "es-p31-altgr");
        checkMapL(0x33, 0, KeyMap::LAYOUT_ES, "\xc3\xb1", 2, "es-p33");
        checkMapL(0x33, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "\xc3\x91", 2,
                  "es-p33-shift");
        checkMapL(0x34, 0, KeyMap::LAYOUT_ES, "\xc2\xb4", 2, "es-p34");
        checkMapL(0x34, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "\xc2\xa8", 2,
                  "es-p34-shift");
        checkMapL(0x34, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "{", 1,
                  "es-p34-altgr");
        checkMapL(0x35, 0, KeyMap::LAYOUT_ES, "\xc2\xba", 2, "es-p35");
        checkMapL(0x35, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "\xc2\xaa", 2,
                  "es-p35-shift");
        checkMapL(0x35, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "\\", 1,
                  "es-p35-altgr");
        checkMapL(0x36, 0, KeyMap::LAYOUT_ES, ",", 1, "es-p36");
        checkMapL(0x36, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, ";", 1,
                  "es-p36-shift");
        checkMapL(0x37, 0, KeyMap::LAYOUT_ES, ".", 1, "es-p37");
        checkMapL(0x37, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, ":", 1,
                  "es-p37-shift");
        checkMapL(0x38, 0, KeyMap::LAYOUT_ES, "-", 1, "es-p38");
        checkMapL(0x38, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, "_", 1,
                  "es-p38-shift");
        checkMapL(0x64, 0, KeyMap::LAYOUT_ES, "<", 1, "es-p64");
        checkMapL(0x64, KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES, ">", 1,
                  "es-p64-shift");
    }

    // ---- ES modifier semantics: LALT = Meta (ESC+char), RALT = AltGr only
    {
        checkMapL(0x04, KeyMap::MOD_LALT, KeyMap::LAYOUT_ES, "\x1b""a", 2,
                  "es-alt-meta");
        // RALT alone produces the AltGr character, not ESC+char
        checkMapL(0x1f, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "@", 1,
                  "es-ralt-not-meta");
        // No AltGr cell for a letter -> nothing
        checkMapL(0x04, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "", 0,
                  "es-ralt-letter-none");
        // AltGr + Shift still resolves the third level (Ctrl > AltGr > Shift)
        checkMapL(0x22, KeyMap::MOD_RALT | KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES,
                  "\xe2\x82\xac", 3, "es-altgr-shift-euro");
        checkMapL(0x1f, KeyMap::MOD_RALT | KeyMap::MOD_LSHIFT, KeyMap::LAYOUT_ES,
                  "@", 1, "es-altgr-shift-at");
        // AltGr on the letter-adjacent ç key yields the pipe (shell symbol)
        checkMapL(0x33, KeyMap::MOD_RALT, KeyMap::LAYOUT_ES, "|", 1,
                  "es-altgr-pipe");
    }

    // ---- ES Ctrl resolves against the AltGr character (Ctrl > AltGr > Shift)
    {
        // AltGr+0x25 -> '[' with LCTRL held -> ESC
        checkMapL(0x25, KeyMap::MOD_RALT | KeyMap::MOD_LCTRL, KeyMap::LAYOUT_ES,
                  "\x1b", 1, "es-ctrl-altgr-lbracket");
        // Ctrl+letter unchanged
        checkMapL(0x04, KeyMap::MOD_LCTRL, KeyMap::LAYOUT_ES, "\x01", 1,
                  "es-ctrl-a");
    }

    if (g_failures == 0) {
        printf("keymap: all tests passed\n");
        return 0;
    }
    printf("keymap: %d FAILURES\n", g_failures);
    return 1;
}
