// esp32Terminal - Vintage CRT terminal on ESP32-S3.
//
// M0: video out (LCD_CAM + GDMA, driver from esp32LanderS3) driving LVGL
// at 8-bit grayscale. LVGL renders into the video framebuffer; the driver
// composes the TV field at EOF (monotonic, no tearing).

#include <Arduino.h>
#include <string.h>
#include <time.h>
#include <esp_heap_caps.h>
#include <esp_pm.h>
#include <esp_private/brownout.h>

#include <lvgl.h>

#include "src/video_s3.h"
#include "src/config.h"
#include "src/term.h"
#include "src/glyph.h"
#include "src/keymap.h"
#include "src/phosphor.h"
#include "src/status.h"
#include "src/audio.h"
#if TERM_SSH_SESSION
#include "src/ssh.h"
#endif
#if TERM_KBD_USB
#include "src/usb_kbd.h"
#else
#include "src/kbd.h"
#endif

static const int XRES = TERM_XRES;
static const int YRES = TERM_YRES;
static const int VIEW_W = TERM_COLS * TERM_CELL_W;
static const int VIEW_H = TERM_ROWS * TERM_CELL_H;

static uint8_t *fb = nullptr;          // driver compose source = glow accumulator
static uint8_t *renderTarget = nullptr; // where LVGL + glyphs draw (fresh frame)

#if TERM_PHOSPHOR
// Persistent phosphor glow (ticket 06): each field the previous content fades
// by TERM_PHOSPHOR_DECAY while freshly drawn pixels re-excite to their own
// level, leaving a P39-style ghost on scroll. The driver composes from `fb`
// (fbShadow); the app draws the fresh frame into a separate `renderTarget`
// and folds it into the glow here, so ghosts can actually decay instead of
// freezing (a shared buffer would keep the ghost bright forever).
static void phosphorFrame(void)
{
    if (renderTarget == nullptr || fb == nullptr) {
        return;
    }
    const size_t n = (size_t)XRES * TERM_YRES;
    for (size_t i = 0; i < n; i++) {
        fb[i] = Phosphor::blend(fb[i], renderTarget[i], TERM_PHOSPHOR_DECAY);
    }
}
#endif

#if TERM_FAKE_SESSION
#include "src/font5x7.h"
static TermGrid grid;
static uint8_t glyphCells[0x60][TERM_CELL_H];
static uint8_t decGlyphCells[0x60][TERM_CELL_H];
static uint8_t statusIconCells[STATUS_ICON_COUNT][TERM_CELL_H];

// DEC special-graphics glyphs (ESC(0), 8x8, MSB = leftmost column), indexed
// by (char - 0x60). Codes follow the VT100 SCS map (j..x = corners/tees on
// scan lines 7..20); undefined slots fall back to a stable blank.
static const uint8_t decGlyphBits[0x60][8] = {
    {0x18, 0x3c, 0x7e, 0xff, 0xff, 0x7e, 0x3c, 0x18}, // ` diamond
    {0x18, 0x3c, 0x76, 0xff, 0xff, 0x76, 0x3c, 0x18}, // a circle +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // b..i undefined
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // e
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // f
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // g
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // h
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // i
    {0x10, 0x10, 0x10, 0xf0, 0x00, 0x00, 0x00, 0x00}, // j scan7   lower-right
    {0x00, 0x00, 0x00, 0xf0, 0x10, 0x10, 0x10, 0x10}, // k scan9   upper-right
    {0x00, 0x00, 0x00, 0x1f, 0x10, 0x10, 0x10, 0x10}, // l scan11  upper-left
    {0x10, 0x10, 0x10, 0x1f, 0x00, 0x00, 0x00, 0x00}, // m scan13  lower-left
    {0x10, 0x10, 0x10, 0xff, 0x10, 0x10, 0x10, 0x10}, // n scan14  box-cross
    {0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00}, // o scan0   horizontal
    {0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00}, // p scan2   horizontal
    {0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00}, // q scan4   horizontal
    {0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00}, // r scan6   horizontal
    {0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00}, // s scan8   horizontal
    {0x10, 0x10, 0x10, 0x1f, 0x10, 0x10, 0x10, 0x10}, // t scan10  left tee
    {0x10, 0x10, 0x10, 0xf0, 0x10, 0x10, 0x10, 0x10}, // u scan12  right tee
    {0x10, 0x10, 0x10, 0xff, 0x00, 0x00, 0x00, 0x00}, // v scan16  bottom tee
    {0x00, 0x00, 0x00, 0xff, 0x10, 0x10, 0x10, 0x10}, // w scan18  top tee
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10}, // x scan20  vertical
    {0x00, 0xc3, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0xff}, // y <=
    {0x00, 0x18, 0x3c, 0x66, 0xc3, 0x66, 0x3c, 0xff}, // z >=
    {0x00, 0xff, 0x24, 0x24, 0x24, 0x24, 0x00, 0x00}, // { pi
    {0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00}, // | not equal
    {0x00, 0x1c, 0x36, 0x06, 0x0f, 0x06, 0x36, 0x1c}, // } pound
    {0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00}, // ~ centered dot
};

static void buildGlyphs(void)
{
    for (int ch = 0x20; ch < 0x80; ch++) {
        const uint8_t *col = font5x7[ch - 0x20];
        uint8_t px[TERM_CELL_H * 5];
        for (int y = 0; y < TERM_CELL_H; y++) {
            for (int x = 0; x < 5; x++) {
                px[y * 5 + x] = (col[x] >> y) & 1 ? 0xff : 0x00;
            }
        }
        uint8_t pxw[TERM_CELL_H * (TERM_CELL_W - 1)];
        collapseColumns(pxw, px, 5, TERM_CELL_H, TERM_CELL_W - 1);
        layoutGlyphBits(glyphCells[ch - 0x20], pxw, TERM_CELL_W - 1,
                        TERM_CELL_H, 0, 0, TERM_CELL_W);
    }
    for (int ch = 0; ch < 0x60; ch++) {
        uint8_t px[TERM_CELL_H * 8];
        for (int y = 0; y < TERM_CELL_H; y++) {
            for (int x = 0; x < 8; x++) {
                px[y * 8 + x] = (decGlyphBits[ch][y] & (0x80 >> x)) ? 0xff
                                                                     : 0x00;
            }
        }
        uint8_t pxw[TERM_CELL_H * TERM_CELL_W];
        collapseColumns(pxw, px, 8, TERM_CELL_H, TERM_CELL_W);
        layoutGlyphBits(decGlyphCells[ch], pxw, TERM_CELL_W, TERM_CELL_H, 0, 0,
                        TERM_CELL_W);
    }
    for (int ic = 0; ic < STATUS_ICON_COUNT; ic++) {
        uint8_t px[TERM_CELL_H * 5];
        for (int y = 0; y < TERM_CELL_H; y++) {
            for (int x = 0; x < 5; x++) {
                px[y * 5 + x] =
                    (statusIconBits[ic][y] & (0x80 >> x)) ? 0xff : 0x00;
            }
        }
        layoutGlyphBits(statusIconCells[ic], px, 5, TERM_CELL_H, 0, 0,
                        TERM_CELL_W);
    }
}

static void renderRowPixels(int r)
{
    const char *row = grid.rowText(r);
    const uint8_t *attr = grid.rowAttr(r);
    for (int c = 0; c < TERM_COLS; c++) {
        uint8_t a = attr[c];
        const uint8_t fg = (a & TERM_ATTR_INVERSE) ? TERM_COLOR_INV_TEXT
                          : ((a & TERM_ATTR_BOLD) ? TERM_COLOR_TEXT_BOLD
                          : ((a & TERM_ATTR_DIM) ? TERM_COLOR_TEXT_DIM
                                                 : TERM_COLOR_TEXT));
        const uint8_t bg = (a & TERM_ATTR_INVERSE) ? TERM_COLOR_INV_BG
                                                    : TERM_COLOR_BG;
        uint8_t cell[TERM_CELL_H];
        if ((a & TERM_ATTR_ALTCHARS) && row[c] >= 0x60 && row[c] <= 0x7e) {
            memcpy(cell, decGlyphCells[row[c] - 0x60], TERM_CELL_H);
        } else if (row[c] >= 0x20 && row[c] < 0x80) {
            memcpy(cell, glyphCells[row[c] - 0x20], TERM_CELL_H);
        } else {
            memset(cell, 0, TERM_CELL_H);
        }
        uint8_t *dst = renderTarget + (uint32_t)(TERM_MARGIN_TOP + r * TERM_CELL_H) * (uint32_t)XRES
                       + (uint32_t)(TERM_MARGIN_X + c * TERM_CELL_W);
        for (int y = 0; y < TERM_CELL_H; y++) {
            uint8_t bits = cell[y];
            for (int x = 0; x < TERM_CELL_W; x++) {
                dst[x] = (bits & (0x80 >> x)) ? fg : bg;
            }
            dst += XRES;
        }
    }
}

static void renderDirtyRows(void)
{
    // The last row is the fixed status bar (ticket 10): it is painted by
    // renderStatusBar() directly into renderTarget, never from the grid, so a
    // host 2J or cursor move to the last row cannot erase it.
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        if (grid.isRowDirty(r)) {
            renderRowPixels(r);
            grid.clearRowDirty(r);
        }
    }
}

static void feedBytes(const uint8_t *data, size_t n)
{
    if (n == 0) return;
    grid.feed(data, n);
}

// Keyboard backend (ticket 04): BLE HID host or USB Host, selected in
// config.h. Both feed the same KeyMap seam; only the transport differs.
#if TERM_KBD_USB
static UsbHost kbd;
#else
static KbdHost kbd;
static bool g_kbdStarted = false;

#if TERM_SSH_SESSION
static void startKbdIfReady(SshState st)
{
    if (g_kbdStarted) {
        return;
    }
    if (st == SSH_STATE_SHELL || st == SSH_STATE_AUTH ||
        st == SSH_STATE_RETRY) {
        g_kbdStarted = true;
        kbd.begin(TERM_KBD_MAC, termKbdKey, termKbdState, termKbdPair);
    }
}
#endif
#endif

static bool g_kbdConnected = false;

// Local echo: a pressed key maps to its terminal byte sequence and is fed
// into the grid (the active session is still the fake one). Enter advances
// the line so a typed line reads like a real shell.
static void termKbdKey(uint8_t keycode, uint8_t mods)
{
#if TERM_CLICK
    // Physical feedback on every key press, regardless of whether the key maps
    // to a byte (a dead/modifier key still clicks).
    Audio::click();
#endif
    static uint8_t seq[8];
    int n = KeyMap::map(keycode, mods, (char *)seq, sizeof(seq),
                        (KeyMap::Layout)TERM_KBD_LAYOUT);
    if (n <= 0) {
        return;
    }
#if TERM_SSH_SESSION
    // With a live SSH session the keys go to the channel (remote echo); in
    // the local password prompt they build the masked password; while
    // connecting/retrying there is no session to type into.
    if (ssh.shellActive()) {
        ssh.sendKey(seq, (size_t)n);
        return;
    }
    if (ssh.authWait()) {
        for (int i = 0; i < n; i++) {
            uint8_t c = seq[i];
            if (c == '\r') {
                ssh.passwordSubmit();
            } else if (c == 0x7f) {
                ssh.passwordBackspace();
            } else if (c >= 0x20 && c <= 0x7e) {
                ssh.passwordChar((char)c);
            }
        }
        statusRedraw(); // refresh the masked prompt on the status bar
        return;
    }
    return; // connecting / retrying: ignore keys
#else
    if (n == 1 && seq[0] == '\r') {
        static const uint8_t nl[2] = {'\r', '\n'};
        grid.feed(nl, 2);
        return;
    }
    grid.feed(seq, (size_t)n);
#endif
}

// --- Status bar (ticket 10 / ADR-0005) ---
// The bar is the fixed bottom grid row (TERM_ROWS-1), painted by the app in
// inverse colors, never through the grid's dirty pass (renderDirtyRows skips
// it). The composition lives in StatusBar (pure, host-tested); this file only
// gathers inputs and paints the row into renderTarget when the content
// changes, so a host 2J or a cursor move to the last row can never erase it.
static char g_barText[TERM_COLS];
static uint8_t g_barIcon[TERM_COLS];
static bool g_barDirty = false;
static bool g_pairingActive = false;
static unsigned long g_pairingPin = 0;
#if TERM_KBD_USB
static char usbDebugLine[48];
static uint8_t usbDebugLen = 0;
#endif
#if TERM_SSH_SESSION
static bool g_timeValid = false;
static int g_tmDay = 0;
static int g_tmMonth = 0;
static int g_tmHour = 0;
static int g_tmMinute = 0;
#endif

static void renderStatusBar(void)
{
    if (renderTarget == nullptr) {
        return;
    }
    const int r = TERM_ROWS - 1;
    const uint8_t fg = TERM_COLOR_INV_TEXT;
    const uint8_t bg = TERM_COLOR_INV_BG;
    uint8_t *base = renderTarget +
                    (uint32_t)(TERM_MARGIN_TOP + r * TERM_CELL_H) *
                        (uint32_t)XRES +
                    TERM_MARGIN_X;
    for (int c = 0; c < TERM_COLS; c++) {
        uint8_t cell[TERM_CELL_H];
        if (g_barIcon[c] != STATUS_ICON_NONE) {
            memcpy(cell, statusIconCells[g_barIcon[c]], TERM_CELL_H);
        } else if (g_barText[c] >= 0x20 && g_barText[c] < 0x80) {
            memcpy(cell, glyphCells[g_barText[c] - 0x20], TERM_CELL_H);
        } else {
            memset(cell, 0, TERM_CELL_H);
        }
        uint8_t *dst = base + (uint32_t)c * TERM_CELL_W;
        for (int y = 0; y < TERM_CELL_H; y++) {
            uint8_t bits = cell[y];
            for (int x = 0; x < TERM_CELL_W; x++) {
                dst[x] = (bits & (0x80 >> x)) ? fg : bg;
            }
            dst += XRES;
        }
    }
    grid.clearRowDirty(TERM_ROWS - 1);
    g_barDirty = false;
#if TERM_KBD_USB
    usbDebugLen = 0;
#endif
}

static void statusRedraw(void)
{
    StatusBar::Inputs in;
    memset(&in, 0, sizeof(in));
#if TERM_SSH_SESSION
    in.state = ssh.state();
    in.sshActive = true;
    in.authWait = ssh.authWait();
    static char mask[22];
    if (in.authWait) {
        ssh.passwordMask(mask, sizeof(mask));
        in.passwordMask = mask;
    }
#endif
    in.pairingActive = g_pairingActive;
    in.pairingPin = g_pairingPin;
#if TERM_KBD_USB
    in.usbDebug = (usbDebugLen > 0) ? usbDebugLine : nullptr;
#endif
    in.kbdPresent = true;
    in.kbdConnected = g_kbdConnected;
#if TERM_SSH_SESSION
    in.showTime = true;
    in.hasTime = g_timeValid;
    in.day = (uint8_t)g_tmDay;
    in.month = (uint8_t)g_tmMonth;
    in.hour = (uint8_t)g_tmHour;
    in.minute = (uint8_t)g_tmMinute;
#endif
    StatusBar::compose(in, g_barText, g_barIcon, sizeof(g_barText));
    g_barDirty = true;
}

// Reflect the keyboard link state on the status bar.
static void termKbdState(bool connected)
{
    g_kbdConnected = connected;
    if (connected) {
        g_pairingActive = false;
    }
    statusRedraw();
}

// During first pairing the keyboard may ask for a code: keep it on the status
// bar (Q11: the pin outranks the password prompt) until the keyboard connects.
#if !TERM_KBD_USB
static void termKbdPair(uint32_t pin)
{
    g_pairingActive = true;
    g_pairingPin = pin;
    statusRedraw();
}
#endif
#endif

// Latest USB host diagnostic text, published from usb_kbd.cpp and drawn on the
// CRT status bar so the host state is visible without a serial monitor (Q11:
// it only shows when no password prompt or pairing pin is active).
#if TERM_KBD_USB
extern "C" void termUsbDebug(const char *text)
{
    size_t n = strlen(text);
    if (n >= sizeof(usbDebugLine)) {
        n = sizeof(usbDebugLine) - 1;
    }
    memcpy(usbDebugLine, text, n);
    usbDebugLine[n] = '\0';
    usbDebugLen = (uint8_t)n;
}
#endif

static void dispFlushCb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    const uint32_t w = (uint32_t)lv_area_get_width(area);
    const uint32_t h = (uint32_t)lv_area_get_height(area);
    uint8_t *dst = renderTarget + (uint32_t)(area->y1 + TERM_MARGIN_TOP) * (uint32_t)XRES
                   + (uint32_t)(area->x1 + TERM_MARGIN_X);
    for (uint32_t y = 0; y < h; y++) {
        memcpy(dst + y * (uint32_t)XRES, px + y * w, w);
    }
    lv_display_flush_ready(d);
}

void setup()
{
    Serial.begin(115200);

    // The USB host powers VBUS for the keyboard; the inrush can trip the
    // brownout detector on an under-powered supply. Disable it so the board
    // does not reset when the keyboard draws current.
    esp_brownout_disable();

    esp_pm_lock_handle_t pmLock;
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "termPerfLock", &pmLock);
    esp_pm_lock_acquire(pmLock);

#if TERM_CLICK
    Audio::begin();
#endif

#if !TERM_TEST_NOVIDEO
    video_graphics_s3();
    fb = video_get_frame_buffer_address();
#else
    fb = (uint8_t *)heap_caps_malloc((size_t)XRES * TERM_YRES, MALLOC_CAP_8BIT);
    if (fb != nullptr) {
        memset(fb, 0, (size_t)XRES * TERM_YRES);
    }
#endif

#if TERM_PHOSPHOR
    // Separate fresh-frame buffer so the glow in `fb` can decay underneath it.
    renderTarget = (uint8_t *)heap_caps_malloc((size_t)XRES * TERM_YRES,
                                               MALLOC_CAP_SPIRAM);
    if (renderTarget == nullptr) {
        Serial.printf("[term] ERROR: no PSRAM para fósforo\n");
        renderTarget = fb; // fall back to pixel-perfect
    } else {
        memset(renderTarget, 0, (size_t)XRES * TERM_YRES);
    }
#else
    renderTarget = fb; // pixel-perfect: draw straight into the driver buffer
#endif

    lv_init();
    lv_display_t *disp = lv_display_create(VIEW_W, VIEW_H);
    static uint8_t dbuf[VIEW_W * 32];
    lv_display_set_buffers(disp, dbuf, nullptr, sizeof(dbuf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, dispFlushCb);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

#if TERM_FAKE_SESSION
    uint8_t *sb = (uint8_t *)heap_caps_malloc(
        (size_t)TERM_SCROLLBACK_LINES * TERM_COLS, MALLOC_CAP_SPIRAM);
    if (sb == nullptr) {
        Serial.printf("[term] ERROR: no PSRAM para scrollback\n");
    }
    grid.init(sb);
    buildGlyphs();

#if TERM_SSH_SESSION
    // No seed text: the SSH session owns the grid. Show a short banner and
    // let the status row report the connection state.
    static const char boot[] = "ESP32 TERMINAL - SSH session\n";
    feedBytes((const uint8_t *)boot, sizeof(boot) - 1);
#else
    static const char seed[] =
        "ESP32 TERMINAL - fake session via USB-serial\n"
        "type on the PC...\n"
        "01234567890123456789012345678901234567890123456789012345\n";
    feedBytes((const uint8_t *)seed, sizeof(seed) - 1);
#endif

    // Keep the session scroll within rows 0..ROWS-2 so the last row stays a
    // fixed status line; then place the cursor at the top of the text area.
    {
        uint8_t region[16];
        int nr = snprintf((char *)region, sizeof(region), "\x1b[1;%dr",
                          TERM_ROWS - 1);
        grid.feed(region, (size_t)(nr > 0 ? nr : 0));
        static const uint8_t home[] = "\x1b[1;1H";
        grid.feed(home, sizeof(home) - 1);
    }

#if TERM_KBD_SCAN
    kbd.scanOnce(TERM_KBD_SCAN_SEC);
#endif

#if TERM_SSH_SESSION
    // Init the BLE radio at boot, BEFORE the SSH task grabs ~120KB of internal
    // DRAM (50KB stack + WiFi + libssh). The BTDM controller carves its memory
    // pool once at init; done late it leaves only ~8KB contiguous and the
    // post-connect ACL allocation fails (BLE_INIT: Malloc failed) -> the peer
    // drops the link (reason 531). Done early the pool comes from the clean
    // 139KB block. The actual keyboard connect is still gated to the SSH state
    // (startKbdIfReady), so no radio traffic happens during the SSH handshake.
    kbd.radioInit();
    ssh.begin();
    statusRedraw();
#endif

#if TERM_KBD_USB
    kbd.begin(termKbdKey, termKbdState);
#else
#if !TERM_SSH_SESSION
    kbd.begin(TERM_KBD_MAC, termKbdKey, termKbdState, termKbdPair);
    g_kbdStarted = true;
#endif
#endif
    statusRedraw();
    Serial.printf("[term] fake session up %dx%d (margins %d,%d,%d)\n",
                  VIEW_W, VIEW_H, TERM_MARGIN_X, TERM_MARGIN_TOP,
                  TERM_MARGIN_BOTTOM);
#elif TERM_LVGL_TEST
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, VIEW_W);
    lv_obj_set_pos(lbl, 0, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_unscii_8, 0);
    lv_label_set_text(lbl,
        "ESP32 TERMINAL 0123456789\n"
        "M0: video + LVGL CRTM$Ba\n"
        "abcdefghijklmnopqrstuvwx\n"
        "ABCDEFGHIJKLMNOPQRSTUVWX\n"
        "The quick brown fox jumps\n"
        "over the lazy dog 12345\n"
        "|`abc de fgh i }} <> []\n"
        "illegal across the board!\n"
        "quick brown fox jumps ovr\n"
        "pack my box with five dz\n"
        "Jived fox nymph grabs quick\n"
        "waltz, bad nymph, for jdk\n"
        "0123456789 ?!@# .,;:''\"-");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    Serial.printf("[lvgl-test] alfombra texto 35x27 (margen 20/12)\n");
#else
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "ESP32 TERMINAL");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "M0: video + LVGL");
    lv_obj_set_style_text_color(sub, lv_color_white(), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 24);

    Serial.printf("[terminal] up %dx%d lvgl=%s\n", XRES, YRES,
                  lv_version_info());
#endif
}

void loop()
{
#if TERM_FAKE_SESSION
#if TERM_SSH_SESSION
    // The SSH session is the feed: drain host bytes into the grid and keep
    // the status row in sync with the connection state.
    uint8_t rbuf[128];
    size_t got = ssh.rxRead(rbuf, sizeof(rbuf));
    if (got > 0) {
        feedBytes(rbuf, got);
    }
    static SshState lastState = (SshState)0xff;
    if (ssh.state() != lastState) {
        lastState = ssh.state();
        if (lastState == SSH_STATE_SHELL) {
            // The session is full-screen now: give the shell a clean grid.
            static const uint8_t clr[] = "\x1b[2J\x1b[H";
            grid.feed(clr, sizeof(clr) - 1);
        } else if (lastState == SSH_STATE_AUTH) {
            // Fresh password prompt: no stale chars or queued password from a
            // previous connection (ADR-0002: typed each time).
            ssh.passwordInit();
        }
        statusRedraw();
    }
#if TERM_SSH_SESSION
    // Status bar clock (ticket 10): NTP time, redrawn only when the minute (or
    // the NTP validity) changes so the bar is not repainted every second. The
    // clock counts as valid once time() is past a boot-epoch threshold (the
    // sntp sync-status enum is transient and unreliable; a real sync jumps the
    // clock to 2026).
    {
        static uint32_t lastSec = 0;
        uint32_t sec = millis() / 1000;
        if (sec != lastSec) {
            lastSec = sec;
            time_t tnow = time(nullptr);
            struct tm tmv;
            localtime_r(&tnow, &tmv);
            int day = tmv.tm_mday;
            int mon = tmv.tm_mon + 1;
            int hr = tmv.tm_hour;
            int min = tmv.tm_min;
            bool valid = tnow > 1500000000; // past ~2017: NTP synced
            if (valid != g_timeValid || min != g_tmMinute ||
                hr != g_tmHour || mon != g_tmMonth || day != g_tmDay) {
                g_timeValid = valid;
                g_tmDay = day;
                g_tmMonth = mon;
                g_tmHour = hr;
                g_tmMinute = min;
                statusRedraw();
            }
        }
    }
#endif
    // Radio is shared (2.4GHz): the BLE keyboard host is started lazily (see
    // startKbdIfReady) once the SSH session is stable, and polled only then —
    // connecting while the SSH task is handshaking drops the pty/shell reply
    // and wedges libssh (SSH_TIMEOUT_INFINITE). AUTH keeps the keyboard live
    // so the CRT password prompt can be typed.
#if TERM_KBD_USB
    if (ssh.state() != SSH_STATE_WIFI) {
        kbd.poll();
    }
#else
    SshState kbdState = ssh.state();
    if (g_kbdStarted && (kbdState == SSH_STATE_SHELL || kbdState == SSH_STATE_AUTH ||
                         kbdState == SSH_STATE_RETRY)) {
        kbd.poll();
    }
#if TERM_SSH_SESSION
    startKbdIfReady(kbdState);
#endif
#endif
#else
    static uint8_t rbuf[64];
    size_t got = 0;
    while (Serial.available() && got < sizeof(rbuf)) {
        rbuf[got++] = (uint8_t)Serial.read();
    }
    feedBytes(rbuf, got);
    kbd.poll();
#endif
#endif

    static uint32_t last = 0;
    uint32_t now = millis();
    uint32_t dt = now - last;
    last = now;
    if (dt > 50) dt = 50;
    lv_tick_inc(dt);

#if TERM_TEST_NOVIDEO
    vTaskDelay(pdMS_TO_TICKS(5));
#else
    video_wait_frame();
#endif
    lv_timer_handler();
#if TERM_FAKE_SESSION
    renderDirtyRows();
#if TERM_KBD_USB
    if (usbDebugLen > 0) {
        statusRedraw();
    }
#endif
    if (g_barDirty) {
        renderStatusBar();
    }
#endif
#if TERM_PHOSPHOR
    phosphorFrame();
#endif
}