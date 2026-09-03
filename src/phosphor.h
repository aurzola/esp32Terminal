#ifndef TERM_PHOSPHOR_H
#define TERM_PHOSPHOR_H

#include <stdint.h>

// Phosphor persistence (ticket 06): each CRT field the previous content fades
// by a fixed-point decay factor while freshly drawn pixels re-excite to their
// own level. Pure and host-testable: no hardware, no LVGL.
namespace Phosphor {

// Fixed-point decay factor in the range 0..256 (256 = no decay, 0 = instant
// fade). 224 (0xE0, ~0.875 per field) leaves a visible ghost for a few tenths
// of a second at 60 fields/sec. Tuneable in config.h (TERM_PHOSPHOR_DECAY).
static inline uint8_t blend(uint8_t glow, uint8_t fresh, uint16_t decay)
{
    if (fresh != 0) {
        return fresh;
    }
    return (uint8_t)(((uint16_t)glow * decay) >> 8);
}

// Apply `blend` to `n` pixels: out[i] = blend(glow[i], fresh[i], decay).
// `out` may alias either input.
static inline void blendRow(uint8_t *out, const uint8_t *glow,
                            const uint8_t *fresh, size_t n, uint16_t decay)
{
    for (size_t i = 0; i < n; i++) {
        out[i] = blend(glow[i], fresh[i], decay);
    }
}

} // namespace Phosphor

#endif