# 07 — Los glifos no se renderizan (LVGL 9.5 devuelve un `lv_draw_buf_t *`)

## Triage: bug (regresión vs. línea base)

**Estado:** resuelto.

## Síntoma

En el CRT no se ve texto legible: bloques/"banderitas" con pocas variantes de
formas (3-4 patrones repetidos), independientemente del carácter. Detectado al
verificar la salida de la app de pantalla completa (ticket 03).

## Cómo se diagnosticó

1. Descartada geometría del driver LCD_CAM+GDMA (fb 320×240, `VIS_Y`/`OFFY`
   coherentes, `fbShadow` en 0 tras init).
2. Dump serial (`[dbg7f0]`) de `glyphCells[]` tras el primer render:
   - los 96 glifos daban patrones casi idénticos: `06 5c 12 40 40` y
     `3a c4 60 40 d0` repetidos, los dígitos 0–9 byte a byte idénticos;
   - `px0` devolvía `19 0e 20 00 08 00 08 00` idéntico para `#`, `0`, `(`, `A`
     → no es un bitmap, es el **header de un struct**.
3. Lee de source de LVGL 9.5
   (`~/Arduino/libraries/lvgl/src/font/fmt_txt/lv_font_fmt_txt.c`):
   `lv_font_get_bitmap_fmt_txt()` termina con `return draw_buf;`.
4. Sonda `db(0)` (`glyphBuf.data`) mostraba el glifo A8 **correcto** (p.ej. `#`
   = `00 ff ff 00 ff ff 00 …`): los datos buenos están en `glyphBuf.data`.

**Causa raíz:** en LVGL 9.5 `lv_font_get_glyph_bitmap()` devuelve
`lv_draw_buf_t *`, no `const uint8_t *`. `buildGlyphs()` lo casteaba y leía los
bytes del struct de ese mismo búfer (idéntico en cada llamada) → tabla de
glifos corrupta desde M1.

## Fix

- `buildGlyphs()` (esp32Terminal.ino) lee el bitmap A8 desde el draw_buf:
  `lv_draw_buf_goto_xy(&glyphBuf, 0, 0)` tras comprobar retorno != nullptr.
- Nuevo helper puro `src/glyph.h` `layoutGlyphBits(cell, px, box_w, box_h,
  ofs_x, ofs_y)`: A8 → 1 bpp MSB-izquierda, honra `box/ofs` con recorte;
  testable en host.
- Regresión en `tests/term/term_test.cpp`: glifo 8×8 columna izquierda
  (sucesión `0x80`), y `#` real de `lv_font_unscii_8` (box 7×7, `ofs (0,1)`,
  A8 reconstruido desde los 7 bytes `6c db fb 6f ed 9b 00` → la columna 8
  (LSB) queda a 0).

## Verificación

- Tests host + ASAN verdes; `build.sh` OK (35% flash / 85% RAM).
- Dump serial post-fix: `cells-lit=94` (solo el espacio en blanco), glifos
  reales distintos (`0`=`3c 66 6e 76 66 66 3c`, `A`=`18 3c 66 66 7e 66 66`,
  `.`=`00 18 18 00 …`); mapa de ocupación del fb muestra las líneas del seed
  (filas 0–4: wrap, espacios en blanco, 35 dígitos) → el framebuffer lleva
  texto legible.
- A confirmar por el usuario mirando al CRT (etapa analógica/composite).

## Nota

`lv_font_unscii_8`: bpp=1, rango `0x20–0x7f` (FORMAT0_TINY, bitmap_format
PLAIN); el decode escribe A8 (`0x00`/`0xff`) en `draw_buf->data` con MSB a la
izquierda. Los glifos en `glyphCells` no usan la columna 8 (la mayoría ocupan
7 px) → la última columna de cada celda queda en 0, patrón esperado.