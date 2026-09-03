#ifndef TERM_KEYMAP_H
#define TERM_KEYMAP_H

#include <stddef.h>
#include <stdint.h>

namespace KeyMap {

// USB HID report modifier bits (byte 0 of a keyboard report). Bit set means
// the corresponding modifier key is held.
enum Modifier : uint8_t {
    MOD_LCTRL  = 0x01,
    MOD_LSHIFT = 0x02,
    MOD_LALT   = 0x04,
    MOD_LGUI   = 0x08,
    MOD_RCTRL  = 0x10,
    MOD_RSHIFT = 0x20,
    MOD_RALT   = 0x40,
    MOD_RGUI   = 0x80,
};

// Whether a Ctrl/Alt modifier is held (either side).
static inline bool hasCtrl(uint8_t mods)
{
    return (mods & (MOD_LCTRL | MOD_RCTRL)) != 0;
}
static inline bool hasAlt(uint8_t mods)
{
    return (mods & (MOD_LALT | MOD_RALT)) != 0;
}
static inline bool hasShift(uint8_t mods)
{
    return (mods & (MOD_LSHIFT | MOD_RSHIFT)) != 0;
}

// Physical keyboard layout that translates HID keycodes (plus modifiers) into
// host bytes. US ANSI is the default; ES (Spanish ISO) shifts the number and
// punctuation rows and adds an AltGr (RALT) third level for shell symbols.
enum Layout : uint8_t {
    LAYOUT_US = 0,
    LAYOUT_ES = 1,
};

// Map a USB HID keyboard usage id (keycode) plus modifier bits to the byte
// sequence that key sends to a host session. Writes at most `cap` bytes into
// `out` and returns the length written; returns 0 for keycodes that map to
// nothing. Multibyte characters (Spanish accent marks, ñ, ç, €) are emitted as
// UTF-8. Defaults to the US ANSI layout.
int map(uint8_t keycode, uint8_t mods, char *out, size_t cap,
        Layout layout = LAYOUT_US);

// Terminal control bytes produced for the common editing keys.
enum : char {
    KEY_ENTER = '\r',
    KEY_BACKSPACE = 0x7f,
    KEY_TAB = '\t',
    KEY_ESC = 0x1b,
};

}

#endif
