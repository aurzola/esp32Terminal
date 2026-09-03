// Host unit tests for TermGrid (no Arduino, no LVGL, no hardware).
// Seam: the public TermGrid interface. Compile on the PC:
//   g++ -std=c++17 -I ../../src -o /tmp/term_test term_test.cpp ../../src/term.cpp

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "term.h"
#include "glyph.h"
#include "font5x7.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

static std::string rowToStr(const char *s, int n)
{
    return std::string(s, s + n);
}

static void feedLine(TermGrid &g, const char *line)
{
    g.feed((const uint8_t *)line, strlen(line));
}

static void feedRaw(TermGrid &g, const char *bytes, size_t n)
{
    g.feed((const uint8_t *)bytes, n);
}

static void feedStr(TermGrid &g, const char *bytes)
{
    g.feed((const uint8_t *)bytes, strlen(bytes));
}

int main(void)
{
    // ---- regression: a line longer than TERM_COLS must wrap, not overflow
    {
        TermGrid g;
        g.init(nullptr);
        std::string longLine(TERM_COLS + 5, 'A'); // 36+ chars, over the edge
        feedLine(g, longLine.c_str());
        // First row holds the first 35 chars; the tail starts at row 1 col 0.
        CHECK(rowToStr(g.rowText(0), TERM_COLS) == longLine.substr(0, TERM_COLS));
        CHECK(g.rowText(1)[0] == 'A');
        CHECK(rowToStr(g.rowText(1), 5) == longLine.substr(TERM_COLS, 5));
        // cursor sits after the wrapped tail (5 chars over the edge)
        CHECK(g.cursorRow() == 1);
        CHECK(g.cursorCol() == 5);
        // no overflow beyond the wrapped rows (grid rows beyond stay blank)
        CHECK(rowToStr(g.rowText(2), TERM_COLS) == std::string(TERM_COLS, ' '));
    }

    // ---- regression: newline() must reset the column, even on a plain LF
    {
        TermGrid g;
        g.init(nullptr);
        feedLine(g, "abc");
        feedLine(g, "\n");
        CHECK(g.cursorRow() == 1);
        CHECK(g.cursorCol() == 0);
        feedLine(g, "XY");
        CHECK(g.cursorRow() == 1);
        CHECK(g.cursorCol() == 2);
        // "XY" landed at the start of row 1, not offset after "abc"
        CHECK(g.rowText(1)[0] == 'X');
        CHECK(g.rowText(1)[1] == 'Y');
    }

    // ---- CR returns to col 0 on the same row, without scrolling
    {
        TermGrid g;
        g.init(nullptr);
        feedLine(g, "abc\rZ");
        CHECK(g.rowText(0)[0] == 'Z');
        CHECK(g.rowText(0)[1] == 'b');
        CHECK(g.rowText(0)[2] == 'c');
        CHECK(g.cursorRow() == 0);
        CHECK(g.cursorCol() == 1);
    }

    // ---- printable range and ignored controls
    {
        TermGrid g;
        g.init(nullptr);
        const uint8_t noise[] = {0x00, 0x07, 0x1b, 0x7f, 'A', '\n'};
        g.feed(noise, sizeof(noise));
        // only 'A' advanced the column; 0x00/0x07/0x1b/0x7f are ignored
        CHECK(g.rowText(0)[0] == 'A');
        CHECK(g.rowText(0)[1] == ' ');
        CHECK(g.cursorCol() == 0); // trailing \n reset it
        CHECK(g.cursorRow() == 1);
    }

    // ---- line feed scrolls the grid and grows the ring buffer (with PSRAM)
    {
        std::vector<uint8_t> scroll(TERM_SCROLLBACK_LINES * TERM_COLS);
        TermGrid g;
        g.init(scroll.data());
        for (int i = 0; i < TERM_ROWS; i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d\n", i);
            feedLine(g, buf);
        }
        // After TERM_ROWS newlines the top line "0" is in the scrollback
        CHECK(g.scrollbackLines() == 1);
        CHECK(rowToStr(g.rowText(0), 2) == "1 "); // scrolled: "1" now on top
        // newest line was pushed off the last row by the final scroll
        CHECK(rowToStr(g.rowText(TERM_ROWS - 1), TERM_COLS) ==
              std::string(TERM_COLS, ' '));
        char lastLine[8];
        snprintf(lastLine, sizeof(lastLine), "%d", TERM_ROWS - 1);
        CHECK(rowToStr(g.rowText(TERM_ROWS - 2), 2) == lastLine);
    }

    // ---- ring buffer caps at TERM_SCROLLBACK_LINES
    {
        std::vector<uint8_t> scroll(TERM_SCROLLBACK_LINES * TERM_COLS);
        TermGrid g;
        g.init(scroll.data());
        // push ~10% more lines than the ring can hold
        const int total = TERM_SCROLLBACK_LINES + TERM_ROWS * 10;
        for (int i = 0; i < total; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "line%d\n", i);
            feedLine(g, buf);
        }
        CHECK(g.scrollbackLines() == TERM_SCROLLBACK_LINES);
        CHECK(g.cursorRow() == TERM_ROWS - 1);
        // ring did not overflow the slice: oldest line is still intact
        CHECK(g.scrollbackLines() <= TERM_SCROLLBACK_LINES);
    }

    // ---- dirty rows track only what changed
    {
        TermGrid g;
        g.init(nullptr);
        CHECK(g.isRowDirty(0) && g.isRowDirty(TERM_ROWS - 1)); // clear() dirties all
        for (int r = 0; r < TERM_ROWS; r++) {
            g.clearRowDirty(r);
        }
        CHECK(!g.isRowDirty(0));
        feedLine(g, "hi\n");
        // rows touched by the feed are dirty; the rest stay clean
        CHECK(g.isRowDirty(0) || g.isRowDirty(1));
        for (int r = 2; r < TERM_ROWS; r++) {
            CHECK(!g.isRowDirty(r));
        }
    }

    // ---- seed from the firmware wraps long title, hides noise, tracks rows
    {
        TermGrid g;
        g.init(nullptr);
        static const char seed[] =
            "ESP32 TERMINAL - fake session via USB-serial\n"
            "type on the PC...\n"
            "01234567890123456789012345678901234567890123456789012345\n";
        size_t seedLen = sizeof(seed) - 1;
        g.feed((const uint8_t *)seed, seedLen);
        // 44-char title: 44 on row 0 (fits 56 cols), no wrap
        std::string seedStr(seed);
        std::string title = std::string(seedStr, 0, 44);
        CHECK(rowToStr(g.rowText(0), TERM_COLS) ==
              title + std::string(TERM_COLS - 44, ' '));
        // 56-char line: fills its row exactly, wraps to the next row
        CHECK(g.rowText(2)[0] == '0');
        CHECK(g.cursorRow() == 3); // seed ends with \n
        CHECK(g.cursorCol() == 0);
    }

    // ---- VT100: cursor home and CUP are 1-based
    {
        TermGrid g;
        g.init(nullptr);
        feedLine(g, "abcdef");
        feedRaw(g, "\x1b[H", 3); // home
        CHECK(g.cursorRow() == 0);
        CHECK(g.cursorCol() == 0);
        feedLine(g, "Z");
        CHECK(g.rowText(0)[0] == 'Z'); // overwrote 'a'
        feedRaw(g, "\x1b[3;5H", 6); // CUP row 3, col 5 (1-based)
        CHECK(g.cursorRow() == 2);
        CHECK(g.cursorCol() == 4);
        feedLine(g, "X");
        CHECK(g.rowText(2)[4] == 'X');
    }

    // ---- VT100: linear cursor movement clamps at the grid edge
    {
        TermGrid g;
        g.init(nullptr);
        feedRaw(g, "\x1b[2;3H", 6); // CUP row 2, col 3 (1-based) → (1,2)
        CHECK(g.cursorRow() == 1);
        CHECK(g.cursorCol() == 2);
        feedRaw(g, "\x1b[C", 3); // fwd
        CHECK(g.cursorRow() == 1);
        CHECK(g.cursorCol() == 3);
        feedRaw(g, "\x1b[D", 3); // back
        CHECK(g.cursorRow() == 1);
        CHECK(g.cursorCol() == 2);
        feedRaw(g, "\x1b[B", 3); // down
        CHECK(g.cursorRow() == 2);
        CHECK(g.cursorCol() == 2);
        feedRaw(g, "\x1b[A", 3); // up
        CHECK(g.cursorRow() == 1);
        CHECK(g.cursorCol() == 2);
        // clamp: up from row 0, left from col 0, right past last col
        for (int i = 0; i < 5; i++) feedRaw(g, "\x1b[A", 3);
        CHECK(g.cursorRow() == 0);
        CHECK(g.cursorCol() == 2);
        for (int i = 0; i < 5; i++) feedRaw(g, "\x1b[D", 3);
        CHECK(g.cursorCol() == 0);
        for (int i = 0; i < TERM_COLS + 2; i++) feedRaw(g, "\x1b[C", 3);
        CHECK(g.cursorCol() == TERM_COLS - 1);
    }

    // ---- VT100: erase display (2J) clears grid and scrollback, keeps cursor
    {
        std::vector<uint8_t> scroll(TERM_SCROLLBACK_LINES * TERM_COLS);
        TermGrid g;
        g.init(scroll.data());
        for (int i = 0; i < TERM_ROWS + 3; i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "line%d\n", i);
            feedLine(g, buf);
        }
        // TERM_ROWS+3 newlines: the first TERM_ROWS-1 move down, the rest scroll
        CHECK(g.scrollbackLines() == 4);
        feedRaw(g, "\x1b[2;5H", 6);
        feedRaw(g, "\x1b[2J", 4);
        CHECK(g.cursorRow() == 1);
        CHECK(g.cursorCol() == 4); // cursor position survives 2J
        CHECK(g.scrollbackLines() == 0); // erased blanks leave the scrollback
        for (int r = 0; r < TERM_ROWS; r++) {
            CHECK(rowToStr(g.rowText(r), TERM_COLS) ==
                  std::string(TERM_COLS, ' '));
        }
        // every erased cell is marked dirty so the widget repaints the blanks
        for (int r = 0; r < TERM_ROWS; r++) {
            CHECK(g.isRowDirty(r));
        }
    }

    // ---- VT100: erase to end of line (0K) blanks from cursor rightward
    {
        TermGrid g;
        g.init(nullptr);
        feedLine(g, "abcdefghij");
        feedRaw(g, "\x1b[7D", 4); // cursor to col 3
        feedRaw(g, "\x1b[K", 3); // erase to EOL
        CHECK(rowToStr(g.rowText(0), 3) == "abc");
        CHECK(rowToStr(g.rowText(0), TERM_COLS) ==
              "abc" + std::string(TERM_COLS - 3, ' '));
        CHECK(g.isRowDirty(0));
        // cursor unchanged after K
        CHECK(g.cursorRow() == 0);
        CHECK(g.cursorCol() == 3);
    }

    // ---- VT100: SGR bold sets per-cell attribute, reset clears it
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "\x1b[1mbold");
        CHECK(g.rowAttr(0)[0] == TERM_ATTR_BOLD);
        CHECK(g.rowAttr(0)[3] == TERM_ATTR_BOLD);
        feedStr(g, "\x1b[0mplain");
        CHECK(g.rowAttr(0)[4] == TERM_ATTR_NONE);
        CHECK(g.rowAttr(0)[8] == TERM_ATTR_NONE);
    }

    // ---- VT100: SGR inverse sets per-cell attribute, reset clears it
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "\x1b[7minv");
        CHECK(g.rowAttr(0)[0] == TERM_ATTR_INVERSE);
        CHECK(g.rowAttr(0)[2] == TERM_ATTR_INVERSE);
        feedStr(g, "\x1b[27mplain");
        CHECK(g.rowAttr(0)[4] == TERM_ATTR_NONE);
        CHECK(g.rowAttr(0)[8] == TERM_ATTR_NONE);
    }

    // ---- VT100: combining bold + inverse sets both bits
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "\x1b[1;7mX");
        CHECK(g.rowAttr(0)[0] == (TERM_ATTR_BOLD | TERM_ATTR_INVERSE));
    }

    // ---- BS (0x08) moves the cursor left so a deletion redraw overwrites the
    // previous cell (fish deletes via cursor-pos + BS + rewrite + erase-EOL).
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "abc");
        feedStr(g, "\x08");      // cursor -> col 2
        CHECK(g.cursorCol() == 2);
        feedStr(g, "X");         // overwrite col 2 (was 'c')
        CHECK(rowToStr(g.rowText(0), 3) == "abX");
        CHECK(g.cursorCol() == 3);
    }

    // ---- fish middle-char deletion: "abxy", cursor after 'y', delete 'x'.
    // fish positions at col 4, BS to col 3, rewrites 'y' (overwrites 'x'),
    // then erases to EOL so the stale 'y' at col 4 disappears.
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "abxy");
        feedStr(g, "\x1b[1D");   // cursor -> col 3 (after 'x')
        feedStr(g, "\x08");      // BS -> col 2
        feedStr(g, "y");         // overwrite col 2 ('x' -> 'y')
        feedStr(g, "\x1b[K");    // erase stale tail to EOL
        CHECK(rowToStr(g.rowText(0), 3) == "aby");
        CHECK(g.cursorRow() == 0);
        CHECK(g.cursorCol() == 3);
    }

    // ---- BS is clamped at column 0 and never wraps or scrolls
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "\x08\x08\x08");
        CHECK(g.cursorRow() == 0);
        CHECK(g.cursorCol() == 0);
    }

    // ---- VT100: SGR dim (2) and bright-black fg (90, fish autosuggestion)
    // set the DIM attribute; reset and SGR 22 clear it
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "\x1b[2mdim");
        CHECK(g.rowAttr(0)[0] == TERM_ATTR_DIM);
        CHECK(g.rowAttr(0)[2] == TERM_ATTR_DIM);
        feedStr(g, "\x1b[0mplain");
        CHECK(g.rowAttr(0)[4] == TERM_ATTR_NONE);
        CHECK(g.rowAttr(0)[8] == TERM_ATTR_NONE);

        feedStr(g, "\x1b[90msugg");
        CHECK(g.rowAttr(0)[9] == TERM_ATTR_DIM);
        feedStr(g, "\x1b[22mnormal");
        CHECK(g.rowAttr(0)[14] == TERM_ATTR_NONE);
    }

    // ---- dim does not bleed after the sequence that resets it
    {
        TermGrid g;
        g.init(nullptr);
        // fish's page reset after a suggestion: bright-black + reset
        feedStr(g, "\x1b[90m-la\x1b[30m\x1b(B\x1b[m");
        CHECK(g.rowAttr(0)[0] == TERM_ATTR_DIM);
        CHECK(g.rowAttr(0)[2] == TERM_ATTR_DIM);
        CHECK(g.rowAttr(0)[3] == TERM_ATTR_NONE); // reset cleared dim
    }

    // ---- VT100: unhandled control bytes pass through without breaking
    {
        TermGrid g;
        g.init(nullptr);
        static const char quirky[] = "abc\x1b]misc\x07xyz\x1bQ";
        feedRaw(g, quirky, sizeof(quirky) - 1);
        CHECK(rowToStr(g.rowText(0), 6) == "abcxyz");
        CHECK(g.cursorRow() == 0);
        CHECK(g.cursorCol() == 6);
    }

    // ---- scroll region (CSI r): LF at the region bottom scrolls only the
    // region; rows outside stay untouched; the region top line goes to
    // the scrollback
    {
        std::vector<uint8_t> scroll(TERM_SCROLLBACK_LINES * TERM_COLS);
        TermGrid g;
        g.init(scroll.data());
        feedRaw(g, "\x1b[2;5r", 6); // region = rows 2..5 (1-based)
        feedRaw(g, "\x1b[2;1H", 6); // cursor to the region top (row 2)
        for (int r = 0; r < TERM_ROWS; r++) {
            g.clearRowDirty(r);
        }
        feedLine(g, "AAAA\n"); // row2
        feedLine(g, "BBBB\n"); // row3
        feedLine(g, "CCCC\n"); // row4: cursor now at the region bottom
        CHECK(g.rowText(1)[0] == 'A');
        CHECK(g.rowText(2)[0] == 'B');
        CHECK(g.rowText(3)[0] == 'C');
        CHECK(g.cursorRow() == 4);
        CHECK(g.cursorCol() == 0);
        feedLine(g, "DDDD\n"); // LF at the region bottom -> scroll the region
        CHECK(g.scrollbackLines() == 1); // region top line "AAAA" pushed
        CHECK(rowToStr(g.rowText(0), TERM_COLS) ==
              std::string(TERM_COLS, ' ')); // row above the region untouched
        CHECK(rowToStr(g.rowText(1), TERM_COLS) ==
              "BBBB" + std::string(TERM_COLS - 4, ' ')); // "AAAA" went to sb
        CHECK(g.rowText(2)[0] == 'C');
        CHECK(g.rowText(3)[0] == 'D');
        CHECK(rowToStr(g.rowText(4), TERM_COLS) ==
              std::string(TERM_COLS, ' ')); // region bottom now blank
        if (TERM_ROWS > 5) {
            CHECK(rowToStr(g.rowText(5), TERM_COLS) ==
                  std::string(TERM_COLS, ' ')); // row below the region
        }
        // a second scroll pushes another line into the scrollback
        feedLine(g, "EEEE\n"); // scroll to row5
        CHECK(g.scrollbackLines() == 2);
        feedLine(g, "FFFF\n"); // another scroll
        CHECK(g.scrollbackLines() == 3);
        CHECK(g.rowText(1)[0] == 'D');
        CHECK(g.rowText(2)[0] == 'E');
        CHECK(g.rowText(3)[0] == 'F');
    }

    // ---- scroll region resets to full screen with no/absent params
    {
        std::vector<uint8_t> scroll(TERM_SCROLLBACK_LINES * TERM_COLS);
        TermGrid g;
        g.init(scroll.data());
        feedRaw(g, "\x1b[3;5r", 6);
        feedRaw(g, "\x1b[r", 3); // no params -> full screen
        for (int i = 0; i < TERM_ROWS; i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "x%d\n", i);
            feedLine(g, buf);
        }
        CHECK(g.scrollbackLines() == 1);
        // single-param / inverted forms also fall back to the full screen
        std::vector<uint8_t> scroll2(TERM_SCROLLBACK_LINES * TERM_COLS);
        TermGrid h;
        h.init(scroll2.data());
        feedRaw(h, "\x1b[5;3r", 6); // bottom < top -> invalid
        for (int i = 0; i < TERM_ROWS; i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "y%d\n", i);
            feedLine(h, buf);
        }
        CHECK(h.scrollbackLines() == 1);
    }

    // ---- scroll region: vertical cursor motion (A/B) clamps inside it
    {
        TermGrid g;
        g.init(nullptr);
        feedRaw(g, "\x1b[3;6r", 6); // region rows 3..6 -> indexes 2..5
        feedRaw(g, "\x1b[6;0H", 6); // CUP below the region (still absolute)
        CHECK(g.cursorRow() == 5);
        feedRaw(g, "\x1b[B", 3); // down from the region bottom: clamped
        CHECK(g.cursorRow() == 5);
        feedRaw(g, "\x1b[4;0H", 6); // CUP to the middle of the region
        for (int i = 0; i < 5; i++) feedRaw(g, "\x1b[A", 3);
        CHECK(g.cursorRow() == 2); // clamped at the region top
    }

    // ---- alternate screen (?1049h): main content frozen, blank alt buffer
    {
        std::vector<uint8_t> scroll(TERM_SCROLLBACK_LINES * TERM_COLS);
        TermGrid g;
        g.init(scroll.data());
        feedLine(g, "MAIN LINE");
        feedLine(g, "\n");
        feedRaw(g, "\x1b[?1049h", 8); // enter alt screen
        // alt starts blank, cursor home
        for (int r = 0; r < TERM_ROWS; r++) {
            CHECK(rowToStr(g.rowText(r), TERM_COLS) ==
                  std::string(TERM_COLS, ' '));
        }
        CHECK(g.cursorRow() == 0);
        CHECK(g.cursorCol() == 0);
        // typing goes to the alt buffer, not the frozen main one
        feedLine(g, "APP UI");
        CHECK(rowToStr(g.rowText(0), 6) == "APP UI");
        feedRaw(g, "\x1b[?1049l", 8); // exit alt screen
        // the main screen is restored exactly, no residue from the app
        CHECK(rowToStr(g.rowText(0), TERM_COLS) ==
              "MAIN LINE" + std::string(TERM_COLS - 9, ' '));
        for (int r = 1; r < TERM_ROWS; r++) {
            CHECK(rowToStr(g.rowText(r), TERM_COLS) ==
                  std::string(TERM_COLS, ' '));
        }
        CHECK(g.cursorRow() == 1);
        CHECK(g.cursorCol() == 0);
    }

    // ---- alternate screen: scrollback is preserved through enter/exit
    {
        std::vector<uint8_t> scroll(TERM_SCROLLBACK_LINES * TERM_COLS);
        TermGrid g;
        g.init(scroll.data());
        for (int i = 0; i < TERM_ROWS; i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "base%d\n", i);
            feedLine(g, buf);
        }
        CHECK(g.scrollbackLines() == 1); // main pushed one line
        feedRaw(g, "\x1b[?1049h", 8);
        // scrolling inside the alt screen must not touch the main scrollback
        for (int i = 0; i < TERM_ROWS + 10; i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "app%d\n", i);
            feedLine(g, buf);
        }
        CHECK(g.scrollbackLines() == 1);
        feedRaw(g, "\x1b[?1049l", 8);
        // main content restored; a normal scroll resumes filling scrollback
        feedLine(g, "after\n");
        CHECK(g.scrollbackLines() == 2);
        CHECK(rowToStr(g.rowText(TERM_ROWS - 2), 5) == "after");
    }

    // ---- alternate screen: 2J inside the alt screen keeps main scrollback
    {
        std::vector<uint8_t> scroll(TERM_SCROLLBACK_LINES * TERM_COLS);
        TermGrid g;
        g.init(scroll.data());
        for (int i = 0; i < TERM_ROWS + 2; i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "line%d\n", i);
            feedLine(g, buf);
        }
        size_t before = g.scrollbackLines();
        CHECK(before == 3);
        feedRaw(g, "\x1b[?1049h", 8);
        feedRaw(g, "\x1b[2J", 4); // full erase inside alt
        CHECK(g.scrollbackLines() == before); // scrollback untouched
        feedRaw(g, "\x1b[?1049l", 8);
        char lastAltLine[10];
        snprintf(lastAltLine, sizeof(lastAltLine), "line%d", TERM_ROWS + 1);
        CHECK(rowToStr(g.rowText(TERM_ROWS - 2), 6) == lastAltLine);
        CHECK(g.cursorRow() == TERM_ROWS - 1);
    }

    // ---- line-drawing: ESC(0 marks cells with TERM_ATTR_ALTCHARS, ESC(B clears
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "\x1b(0");
        feedStr(g, "qjx");
        CHECK(g.rowAttr(0)[0] == TERM_ATTR_ALTCHARS); // q -> horizontal line
        CHECK(g.rowAttr(0)[1] == TERM_ATTR_ALTCHARS); // j -> lower-right corner
        CHECK(g.rowAttr(0)[2] == TERM_ATTR_ALTCHARS);
        feedStr(g, "\x1b(Bascii");
        CHECK(g.rowAttr(0)[3] == TERM_ATTR_NONE); // back to ASCII charset
        CHECK(g.rowText(0)[3] == 'a');
        // the DEC codes pass the seam intact for the renderer to map
        CHECK(g.rowText(0)[0] == 'q');
        CHECK(g.rowText(0)[1] == 'j');
        CHECK(g.rowText(0)[2] == 'x');
    }

    // ---- line-drawing: charset survives across feeds and never drops grid
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "\x1b");
        feedStr(g, "(0"); // ESC and (0 split across two feeds
        feedStr(g, "lm");
        CHECK(g.rowAttr(0)[0] == TERM_ATTR_ALTCHARS);
        CHECK(g.rowAttr(0)[1] == TERM_ATTR_ALTCHARS);
        // control chars stay inert even in the alternate charset
        feedStr(g, "\x1b(1"); // unknown charset -> dropped, stays ascii-ish
        CHECK(g.rowText(0)[2] == '\0' || g.rowText(0)[2] == ' ');
        feedStr(g, "ab");
        CHECK(g.rowAttr(0)[2] == TERM_ATTR_ALTCHARS); // alt still active after ESC(1
        CHECK(g.rowText(0)[2] == 'a');
        CHECK(g.rowText(0)[3] == 'b');
    }

    // ---- line-drawing: erasing a drawn cell resets its attr, and SGR
    // combines with the alternate charset
    {
        TermGrid g;
        g.init(nullptr);
        feedStr(g, "\x1b[1m");
        feedStr(g, "\x1b(0q");
        CHECK(g.rowAttr(0)[0] == (TERM_ATTR_BOLD | TERM_ATTR_ALTCHARS));
        feedStr(g, "\x1b[D"); // back a column
        feedStr(g, "\x1b[K"); // erase to EOL
        // an erased cell keeps the current SGR but drops the alt charset
        CHECK(g.rowAttr(0)[0] == TERM_ATTR_BOLD);
        CHECK((g.rowAttr(0)[0] & TERM_ATTR_ALTCHARS) == 0);
        CHECK(rowToStr(g.rowText(0), TERM_COLS) ==
              std::string(TERM_COLS, ' '));
    }

    // ---- glyph layout: A8 pixels -> 1bpp cell, MSB = leftmost column,
    // honoring box/ofs (LVGL 9.5 returns the draw_buf; the A8 rows must be
    // read from the draw_buf, not from the returned pointer)
    {
        uint8_t px[64] = {};
        for (int y = 0; y < 8; y++) px[y * 8] = 0xff; // 8x8, left column
        uint8_t cell[8];
        layoutGlyphBits(cell, px, 8, 8, 0, 0, 8);
        const uint8_t want0[8] = {0x80, 0x80, 0x80, 0x80,
                                  0x80, 0x80, 0x80, 0x80};
        CHECK(memcmp(cell, want0, 8) == 0);

        const uint8_t hashRaw[7] = {0x6c, 0xdb, 0xfb, 0x6f, 0xed, 0x9b, 0x00};
        uint8_t px7[7 * 7] = {};
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 7; x++) {
                px7[y * 7 + x] = (hashRaw[y] & (0x80 >> x)) ? 0xff : 0x00;
            }
        }
        layoutGlyphBits(cell, px7, 7, 7, 0, 1, 8);
        const uint8_t want1[8] = {0x00, 0x6c, 0xda, 0xfa,
                                  0x6e, 0xec, 0x9a, 0x00};
        CHECK(memcmp(cell, want1, 8) == 0);
    }

    // ---- font5x7: columns -> 5x8 cell placed at ofs (1,0); bit j = row j
    {
        uint8_t cell[8];
        const uint8_t *col = font5x7['A' - 0x20];
        uint8_t px[8 * 5];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 5; x++)
                px[y * 5 + x] = (col[x] >> y) & 1 ? 0xff : 0x00;
        layoutGlyphBits(cell, px, 5, 8, 1, 0, 8);
        const uint8_t wantA[8] = {0x10, 0x28, 0x44, 0x44,
                                  0x7C, 0x44, 0x44, 0x00};
        CHECK(memcmp(cell, wantA, 8) == 0);
        // a descender glyph (g) must reach the bottom row of the 8x8 cell
        col = font5x7['g' - 0x20];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 5; x++)
                px[y * 5 + x] = (col[x] >> y) & 1 ? 0xff : 0x00;
        layoutGlyphBits(cell, px, 5, 8, 1, 0, 8);
        CHECK((cell[7] & (0x80 >> 1 | 0x80 >> 2 | 0x80 >> 3)) != 0);
    }

    // ---- collapseColumns 5->4: the widest glyphs (M, W use all 5 columns)
    // must keep their ink: no source column is dropped, only col0|col1 merge.
    // The collapsed 4-px glyph sits at ofs 0 in a 5-px cell: bit 4 (0x08)
    // stays 0, the right gutter that keeps adjacent glyphs from fusing.
    {
        // M = {7f,02,1c,02,7f}: col0|col1 = 0x7f, so the left stroke survives
        // intact through the collapse (0x80 column on every cap row).
        uint8_t pxM[8 * 5];
        const uint8_t *colM = font5x7['M' - 0x20];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 5; x++)
                pxM[y * 5 + x] = (colM[x] >> y) & 1 ? 0xff : 0x00;
        uint8_t cellM[8];
        uint8_t pxc[8 * 4];
        collapseColumns(pxc, pxM, 5, 8, 4);
        layoutGlyphBits(cellM, pxc, 4, 8, 0, 0, 5);
        const uint8_t wantM[4] = {0x90, 0xB0, 0xD0, 0xD0};
        CHECK(memcmp(cellM, wantM, 4) == 0);
        CHECK(cellM[5] == 0x90); // bottom row of the left stroke still lit
        CHECK(cellM[6] == 0x90);
        CHECK(cellM[7] == 0x00);
        for (int r = 0; r < 8; r++) {
            CHECK((cellM[r] & 0x08) == 0); // gutter bit always clear
        }
        // W = {3f,40,38,40,3f}: col0|col1 = 0x7f keeps both outer strokes
        uint8_t pxW[8 * 5];
        const uint8_t *colW = font5x7['W' - 0x20];
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 5; x++)
                pxW[y * 5 + x] = (colW[x] >> y) & 1 ? 0xff : 0x00;
        uint8_t cellW[8];
        collapseColumns(pxc, pxW, 5, 8, 4);
        layoutGlyphBits(cellW, pxc, 4, 8, 0, 0, 5);
        const uint8_t wantW[4] = {0x90, 0x90, 0x90, 0xD0};
        CHECK(memcmp(cellW, wantW, 4) == 0);
        CHECK(cellW[5] == 0xD0);
        CHECK(cellW[6] == 0xA0);
        CHECK(cellW[7] == 0x00);
    }

    // ---- collapseColumns 8->5 for the DEC line-drawing set: a full-width
    // horizontal line (0xff on its row) must stay full-width at 5px so boxes
    // connect across cells, and may use the gutter bit (0x08) unlike text.
    {
        uint8_t px[8 * 8] = {};
        for (int x = 0; x < 8; x++) px[3 * 8 + x] = 0xff; // row 3 full line
        uint8_t pxc[8 * 5];
        collapseColumns(pxc, px, 8, 8, 5);
        uint8_t cell[8];
        layoutGlyphBits(cell, pxc, 5, 8, 0, 0, 5);
        const uint8_t want[4] = {0x00, 0x00, 0x00, 0xF8};
        CHECK(memcmp(cell, want, 4) == 0);
        CHECK((cell[3] & 0x08) != 0); // DEC uses the gutter column
    }

    if (g_failures == 0) {
        printf("term: all tests passed\n");
        return 0;
    }
    printf("term: %d FAILURES\n", g_failures);
    return 1;
}