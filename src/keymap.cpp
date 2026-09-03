#include "keymap.h"

#include <cstring>

namespace {

// A printable character selected from a key: either a single ASCII byte or a
// multibyte UTF-8 string. `n` is the byte length (0 = no printable on this key).
struct Sel {
    const char *bytes;
    int n;
};

// Unshifted / shifted pairs for the punctuation keys (US ANSI), indexed by
// keycode - 0x2d so [0] is 0x2d, [1] is 0x2e, etc.
static const char kPunct[][2] = {
    {'-', '_'}, // 0x2d
    {'=', '+'}, // 0x2e
    {'[', '{'}, // 0x2f
    {']', '}'}, // 0x30
    {'\\', '|'},// 0x31
    {' ', ' '}, // 0x32 (reserved)
    {';', ':'}, // 0x33
    {'\'', '"'},// 0x34
    {'`', '~'}, // 0x35
    {',', '<'}, // 0x36
    {'.', '>'}, // 0x37
    {'/', '?'}, // 0x38
};

// Multibyte Spanish characters, encoded as UTF-8. Indexed by 0x80 + offset so
// a single-byte table cell can reference them without ambiguity.
static const char *const kMulti[] = {
    "\xc2\xa1",          // 0x80: ¡
    "\xc2\xbf",          // 0x81: ¿
    "\xc2\xa8",          // 0x82: ¨
    "\xc2\xb7",          // 0x83: ·
    "\xc3\xa7",          // 0x84: ç
    "\xc3\x87",          // 0x85: Ç
    "\xc3\xb1",          // 0x86: ñ
    "\xc3\x91",          // 0x87: Ñ
    "\xc2\xb4",          // 0x88: ´
    "\xc2\xba",          // 0x89: º
    "\xc2\xaa",          // 0x8a: ª
    "\xe2\x82\xac",      // 0x8b: €
    "\xc2\xac",          // 0x8c: ¬
};

// Resolve a table cell to a Sel. Cells are single ASCII chars (>= 0x01), a
// multibyte reference (>= 0x80 -> kMulti), or 0 (no glyph on this level).
static Sel cellSel(uint8_t cell)
{
    if (cell >= 0x80) {
        return Sel{kMulti[cell - 0x80], (int)strlen(kMulti[cell - 0x80])};
    }
    if (cell == 0) {
        return Sel{nullptr, 0};
    }
    static char single[1];
    single[0] = (char)cell;
    return Sel{single, 1};
}

// Number row (HID 0x1e..0x27) for the ES layout: {base, shift, altgr}.
static const uint8_t kESNum[][3] = {
    {'1', '!', '|'},           // 0x1e
    {'2', '"', '@'},           // 0x1f
    {'3', 0x83, '#'},          // 0x20 (shift = ·)
    {'4', '$', '~'},           // 0x21
    {'5', '%', 0x8b},          // 0x22 (altgr = €)
    {'6', '&', 0x8c},          // 0x23 (altgr = ¬)
    {'7', '/', '{'},           // 0x24
    {'8', '(', '['},           // 0x25
    {'9', ')', ']'},           // 0x26
    {'0', '=', '}'},           // 0x27
};

// Symbol row for the ES layout, keyed by HID keycode. `altgr` is the resolved
// right-Alt (RALT) state passed in by the caller.
static Sel esSymbol(uint8_t keycode, bool altgr, bool shift)
{
    if (altgr) {
        switch (keycode) {
        case 0x2d: return cellSel('\\'); // backslash
        case 0x2f: return cellSel('[');  // lbracket
        case 0x30: return cellSel(']');  // rbracket
        case 0x31: return cellSel('}');  // rbrace
        case 0x33: return cellSel('|');  // pipe
        case 0x34: return cellSel('{');  // lbrace
        case 0x35: return cellSel('\\'); // backslash
        default:   return Sel{nullptr, 0};
        }
    }
    switch (keycode) {
    case 0x2d: return cellSel(shift ? '?' : '\'');
    case 0x2e: return cellSel(shift ? 0x81 : 0x80); // ¿ / ¡
    case 0x2f: return cellSel(shift ? 0x82 : '`'); // ¨ / `
    case 0x30: return cellSel(shift ? '*' : '+');
    case 0x31: return cellSel(shift ? 0x85 : 0x84); // Ç / ç
    case 0x33: return cellSel(shift ? 0x87 : 0x86); // Ñ / ñ
    case 0x34: return cellSel(shift ? 0x82 : 0x88); // ¨ / ´
    case 0x35: return cellSel(shift ? 0x8a : 0x89); // ª / º
    case 0x36: return cellSel(shift ? ';' : ','); // semicolon / comma
    case 0x37: return cellSel(shift ? ':' : '.'); // colon / period
    case 0x38: return cellSel(shift ? '_' : '-'); // underscore / dash
    case 0x64: return cellSel(shift ? '>' : '<'); // greater / less
    default:   return Sel{nullptr, 0};
    }
}

// Select the printable character for a key, honoring the layout and modifier
// priority Ctrl > AltGr > Shift on ES. AltGr is the right Alt (RALT).
static Sel selectChar(uint8_t keycode, uint8_t mods, KeyMap::Layout layout)
{
    bool shift = KeyMap::hasShift(mods);
    bool altgr = (mods & KeyMap::MOD_RALT) != 0;
    // AltGr (RALT) has no third level on the ES letter row: yield nothing.
    if (layout == KeyMap::LAYOUT_ES && altgr && keycode >= 0x04 &&
        keycode <= 0x1d) {
        return Sel{nullptr, 0};
    }
    if (keycode == 0x2c) {
        return cellSel(' '); // Space
    }
    // Letters resolve identically in both layouts.
    if (keycode >= 0x04 && keycode <= 0x1d) {
        char c = (char)('a' + (keycode - 0x04));
        if (shift) {
            c = (char)(c - 'a' + 'A');
        }
        return cellSel(c);
    }
    if (layout == KeyMap::LAYOUT_ES) {
        if (keycode >= 0x1e && keycode <= 0x27) {
            const uint8_t *row = kESNum[keycode - 0x1e];
            return cellSel(altgr ? row[2] : (shift ? row[1] : row[0]));
        }
        return esSymbol(keycode, altgr, shift);
    }
    // US ANSI
    if (keycode >= 0x1e && keycode <= 0x27) {
        static const char shifted[] = ")!@#$%^&*(";
        char base = (char)('1' + (keycode - 0x1e));
        if (base > '9') {
            base = '0';
        }
        if (shift) {
            base = shifted[(int)(base - '0')];
        }
        return cellSel(base);
    }
    if (keycode >= 0x2d && keycode <= 0x38) {
        char c = kPunct[keycode - 0x2d][shift ? 1 : 0];
        if (c == ' ') {
            return Sel{nullptr, 0}; // reserved slot, no key
        }
        return cellSel(c);
    }
    return Sel{nullptr, 0};
}

} // namespace

int KeyMap::map(uint8_t keycode, uint8_t mods, char *out, size_t cap,
                Layout layout)
{
    if (cap < 5) {
        return 0;
    }

    Sel sel = selectChar(keycode, mods, layout);
    if (sel.n != 0) {
        char base = sel.bytes[0];
        // Ctrl turns letters into control chars; a few punctuation keys map
        // to control chars too. Ctrl on any other printable (digits, space,
        // most symbols, or a multibyte char) has no terminal meaning.
        if (KeyMap::hasCtrl(mods)) {
            if (sel.n == 1) {
                if (base >= 'a' && base <= 'z') {
                    out[0] = (char)(base - 'a' + 1);
                    return 1;
                }
                if (base >= 'A' && base <= 'Z') {
                    out[0] = (char)(base - 'A' + 1);
                    return 1;
                }
                switch (base) {
                case '[': out[0] = 0x1b; return 1; // ESC
                case '\\': out[0] = 0x1c; return 1; // FS
                case ']': out[0] = 0x1d; return 1;  // GS
                default:
                    break;
                }
            }
            return 0;
        }
        // Alt (Meta) prefixes the character with ESC. On US, either Alt key
        // is Meta; on ES, only the left Alt is Meta (right Alt is AltGr).
        bool meta;
        if (layout == LAYOUT_ES) {
            meta = (mods & MOD_LALT) != 0;
        } else {
            meta = KeyMap::hasAlt(mods);
        }
        if (meta && !KeyMap::hasCtrl(mods)) {
            if (cap < 2 + (size_t)sel.n) {
                return 0;
            }
            out[0] = KEY_ESC;
            memcpy(out + 1, sel.bytes, (size_t)sel.n);
            return 1 + sel.n;
        }
        if (cap < (size_t)sel.n) {
            return 0;
        }
        memcpy(out, sel.bytes, (size_t)sel.n);
        return sel.n;
    }

    // Basic editing keys.
    switch (keycode) {
    case 0x28: out[0] = KEY_ENTER; return 1;// Enter -> CR
    case 0x2a: out[0] = KEY_BACKSPACE; return 1; // Backspace -> DEL
    case 0x2b: out[0] = KEY_TAB; return 1;  // Tab -> HT
    case 0x29: out[0] = KEY_ESC; return 1;  // Esc
    default:
        break;
    }
    // Navigation keys: arrows/steps as CSI sequences (xterm style).
    switch (keycode) {
    case 0x52: out[0] = KEY_ESC; out[1] = '['; out[2] = 'A'; return 3; // up
    case 0x51: out[0] = KEY_ESC; out[1] = '['; out[2] = 'B'; return 3; // down
    case 0x4f: out[0] = KEY_ESC; out[1] = '['; out[2] = 'C'; return 3; // right
    case 0x50: out[0] = KEY_ESC; out[1] = '['; out[2] = 'D'; return 3; // left
    case 0x4a: out[0] = KEY_ESC; out[1] = '['; out[2] = 'H'; return 3; // home
    case 0x4d: out[0] = KEY_ESC; out[1] = '['; out[2] = 'F'; return 3; // end
    case 0x4b: out[0] = KEY_ESC; out[1] = '['; out[2] = '5'; out[3] = '~'; return 4; // pgup
    case 0x4e: out[0] = KEY_ESC; out[1] = '['; out[2] = '6'; out[3] = '~'; return 4; // pgdn
    case 0x49: out[0] = KEY_ESC; out[1] = '['; out[2] = '2'; out[3] = '~'; return 4; // ins
    case 0x4c: out[0] = KEY_ESC; out[1] = '['; out[2] = '3'; out[3] = '~'; return 4; // del
    default:
        break;
    }
    // Function keys: F1-F4 use SS3 (ESC O P..S), F5-F12 use CSI ~.
    if (keycode >= 0x58 && keycode <= 0x5b) {
        static const char ss3[] = "PQRS";
        out[0] = KEY_ESC;
        out[1] = 'O';
        out[2] = ss3[keycode - 0x58];
        return 3;
    }
    if (keycode >= 0x5c && keycode <= 0x63) {
        static const char csip[] = {15, 17, 18, 19, 20, 21, 23, 24};
        int n = csip[keycode - 0x5c];
        out[0] = KEY_ESC;
        out[1] = '[';
        out[2] = (char)('0' + (n / 10));
        out[3] = (char)('0' + (n % 10));
        out[4] = '~';
        return 5;
    }
    out[0] = '\0';
    return 0;
}
