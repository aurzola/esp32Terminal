#ifndef TERM_GLYPH_H
#define TERM_GLYPH_H

#include <stdint.h>
#include <string.h>

static inline void layoutGlyphBits(uint8_t *cell, const uint8_t *px, int box_w,
                                   int box_h, int ofs_x, int ofs_y, int cell_w)
{
    for (int i = 0; i < 8; i++) cell[i] = 0;
    for (int y = 0; y < box_h; y++) {
        int cy = ofs_y + y;
        if (cy < 0 || cy >= 8) continue;
        for (int x = 0; x < box_w; x++) {
            int cx = ofs_x + x;
            if (cx < 0 || cx >= cell_w) continue;
            if (px[y * box_w + x] > 0x80) {
                cell[cy] |= (uint8_t)(0x80u >> cx);
            }
        }
    }
}

// Collapse a box_w-wide bitmap into cell_w columns by OR-ing source columns
// into their destination (dst[ s*cell_w/box_w ] |= src[s]). Deterministic:
// 5->4 merges [0|1,2,3,4], 8->4 merges pairs. No source column is dropped.
static inline void collapseColumns(uint8_t *dst, const uint8_t *px, int box_w,
                                   int box_h, int cell_w)
{
    for (int y = 0; y < box_h; y++) {
        for (int x = 0; x < cell_w; x++) dst[y * cell_w + x] = 0;
        for (int x = 0; x < box_w; x++) {
            int dx = x * cell_w / box_w;
            dst[y * cell_w + dx] |= px[y * box_w + x];
        }
    }
}

#endif