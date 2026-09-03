// Host unit tests for the phosphor decay blend (no Arduino, no hardware).
// Seam: the pure per-pixel blend used to compose each CRT field. Compile:
//   g++ -std=c++17 -I ../../src -o /tmp/phosphor_test phosphor_test.cpp

#include <cstdio>

#include "phosphor.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

int main(void)
{
    // ---- fresh content re-excites the phosphor to its own level
    {
        // a just-drawn bright pixel shows full brightness regardless of glow
        CHECK(Phosphor::blend(0, 255, 0xE0) == 255);
        // a just-drawn dim pixel wins over a weaker ghost (re-excitation)
        CHECK(Phosphor::blend(0, 190, 0xE0) == 190);
        // re-excitation beats an older, weaker glow
        CHECK(Phosphor::blend(100, 190, 0xE0) == 190);
        // an already-bright pixel keeps its brightness
        CHECK(Phosphor::blend(255, 255, 0xE0) == 255);
    }

    // ---- a pixel that is no longer drawn decays by the fixed-point factor
    {
        // 0xE0 = 224/256: one field decays 255 to ~223
        CHECK(Phosphor::blend(255, 0, 0xE0) == 223);
        // repeated decay converges toward 0 without overshooting
        uint8_t glow = 255;
        for (int i = 0; i < 40; i++) {
            glow = Phosphor::blend(glow, 0, 0xE0);
        }
        CHECK(glow < 4);
        // monotonic: each field is never brighter than the previous ghost
        uint8_t prev = 255;
        for (int i = 0; i < 20; i++) {
            uint8_t next = Phosphor::blend(prev, 0, 0xE0);
            CHECK(next <= prev);
            prev = next;
        }
    }

    // ---- decay 0 = pixel-perfect instant fade of unwritten pixels
    {
        CHECK(Phosphor::blend(255, 0, 0) == 0);
        CHECK(Phosphor::blend(0, 190, 0) == 190);
    }

    // ---- decay 256 = no decay at all (persist forever)
    {
        CHECK(Phosphor::blend(255, 0, 256) == 255);
        CHECK(Phosphor::blend(17, 0, 256) == 17);
    }

    // ---- a full row apply leaves unwritten ghosts and fresh text intact
    {
        uint8_t glow[8] = {0, 255, 255, 255, 0, 0, 0, 0};
        uint8_t fresh[8] = {0, 0, 190, 0, 255, 0, 190, 0};
        uint8_t out[8];
        Phosphor::blendRow(out, glow, fresh, 8, 0xE0);
        CHECK(out[0] == 0);               // unwritten, no glow
        CHECK(out[1] == 223);             // unwritten, glow decays
        CHECK(out[2] == 190);             // re-excited by fresh text
        CHECK(out[3] == 223);             // unwritten, glow decays
        CHECK(out[4] == 255);             // fresh text wins
        CHECK(out[5] == 0);               // nothing anywhere
        CHECK(out[6] == 190);             // fresh text wins
        CHECK(out[7] == 0);
    }

    if (g_failures == 0) {
        printf("phosphor: all tests passed\n");
        return 0;
    }
    printf("phosphor: %d FAILURES\n", g_failures);
    return 1;
}