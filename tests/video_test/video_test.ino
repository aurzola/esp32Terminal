// video_test - aislamiento de capas para el CRT.
// Patrones deterministas escritos DIRECTOS en fbShadow, sin LVGL.
// Si estos patrones salen rotos -> driver/hardware. Si salen perfectos -> capa LVGL.
// Incluye el driver inline desde src/ para no duplicar codigo.

// Use only via the Diagnosing Bugs skill; throwaway harness (delete after use).

#include <Arduino.h>
#include "../../src/video_s3.cpp"

static const uint8_t GLYPH_E[8] = {0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0xF8, 0x00};

static int gW, gH;

static void fillRect(int x, int y, int w, int h, uint8_t v)
{
    uint8_t *fb = video_get_frame_buffer_address();
    for (int j = 0; j < h; j++) {
        memset(fb + (y + j) * gW + x, v, w);
    }
}

static void drawPhase(uint8_t phase)
{
    uint8_t *fb = video_get_frame_buffer_address();
    switch (phase) {
    case 0: /* SOLID BLACK */
        memset(fb, 0, (size_t)gW * gH);
        break;
    case 1: /* SOLID WHITE */
        memset(fb, 255, (size_t)gW * gH);
        break;
    case 2: /* MID GRAY */
        memset(fb, 128, (size_t)gW * gH);
        break;
    case 3: /* 1px HORIZONTAL LINES: rows alternate */
        for (int y = 0; y < gH; y++) {
            memset(fb + (size_t)y * gW, (y & 1) ? 255 : 0, gW);
        }
        break;
    case 4: /* 1px VERTICAL LINES: cols alternate */
        for (int y = 0; y < gH; y++) {
            for (int x = 0; x < gW; x++) {
                fb[(size_t)y * gW + x] = (x & 1) ? 255 : 0;
            }
        }
        break;
    case 5: /* CHECKERED 8x8 CELLS: simula glyphs a resolucion de celda */
        for (int y = 0; y < gH; y++) {
            for (int x = 0; x < gW; x++) {
                bool cell = ((y >> 3) ^ (x >> 3)) & 1;
                uint8_t v = cell ? (((x ^ y) & 1) ? 255 : 64) : 0;
                fb[(size_t)y * gW + x] = v;
            }
        }
        break;
    case 6: /* GRAY STAIRCASE: 16 bandas verticales */
        for (int y = 0; y < gH; y++) {
            for (int x = 0; x < gW; x++) {
                fb[(size_t)y * gW + x] = (uint8_t)((x * 16) / gW * 16);
            }
        }
        break;
    case 7: /* GLYPH E repetido en grid 8x8 con 2px de separacion */
        memset(fb, 0, (size_t)gW * gH);
        for (int gr = 0; gr < 8; gr++) {
            uint8_t px = GLYPH_E[gr];
            for (int gy = gr; gy < gH; gy += 10) {
                for (int gx = 0; gx < gW; gx += 10) {
                    for (int b = 0; b < 8; b++) {
                        if (px & (0x80 >> b)) {
                            fb[(size_t)gy * gW + gx + b] = 255;
                        }
                    }
                }
            }
        }
        break;
    case 8: /* OVERSCAN v2: fondo blanco, marco negro 2px + púas negras 16px */
        memset(fb, 255, (size_t)gW * gH);
        fillRect(0, 0, gW, 2, 0);        /* marco superior */
        fillRect(0, gH - 2, gW, 2, 0);   /* marco inferior */
        fillRect(0, 0, 2, gH, 0);        /* marco izquierdo */
        fillRect(gW - 2, 0, 2, gH, 0);   /* marco derecho */
        for (int x = 16; x < gW - 16; x += 16) {
            fillRect(x, 2, 2, 12, 0);     /* pua superior */
            fillRect(x, gH - 14, 2, 12, 0);/* pua inferior */
        }
        for (int y = 16; y < gH - 16; y += 16) {
            fillRect(2, y, 12, 2, 0);     /* pua izquierda */
            fillRect(gW - 14, y, 12, 2, 0);/* pua derecha */
        }
        break;
    }
}

static uint32_t dwellMs(uint8_t phase)
{
    return 10000;
}

void setup()
{
    Serial.begin(115200);
    video_graphics_s3();
    gW = video_width();
    gH = video_height();
    Serial.printf("[video_test] %dx%d modo overscan fijo\n", gW, gH);
}

void loop()
{
    static uint32_t next = 0;
    uint32_t now = millis();
    if (now >= next) {
        drawPhase(8);
        Serial.printf("[video_test] FASE 8: marco 1px + ticks cada 16px\n");
        next = now + 10000;
    }
    video_wait_frame();
}