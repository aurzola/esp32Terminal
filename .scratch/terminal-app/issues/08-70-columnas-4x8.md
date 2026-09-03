# 08 — 70 columnas a celdas 4×8 px (ADR-0004)

**What to build:** Llevar el grid de 35 columnas × 26 filas a celdas de 8 px a
**70 columnas × 26 filas a celdas de 4×8 px**, manteniendo la banda segura de
280 px (márgenes 20/20) que M1 blindó contra el overscan. Los glifos 5×7 se
colapsan a 4 px con una regla determinista (`collapseColumns`) y las cajas
line-drawing DEC pasan a 4 px manteniendo las líneas rectas a ancho completo.

**Blocked by:** 03

**Status:** resolved

- [x] `layoutGlyphBits` parametrizado con `cell_w`; nuevo helper puro
      `collapseColumns` en `src/glyph.h` (colapso OR `dst[s*cell_w/box_w] |= src[s]`).
- [x] `TERM_COLS 70`, `TERM_CELL_W 4`, `TERM_CELL_H 8` en `config.h`; márgenes
      derivados de las macros (banda 280 px, idéntica al layout anterior).
- [x] `buildGlyphs()` colapsa texto 5→4 y DEC 8→4; `renderRowPixels` y
      `VIEW_W/H` parametrizados; seed de fake-session a 70 dígitos.
- [x] Tests host: colapso conserva tinta en M/W, línea horizontal DEC a ancho
      completo, seed 70 col. Todo verde (incl. ASAN).
- [x] ADR-0004 y CONTEXT.md actualizados: 70 como objetivo, "80" como frontera
      pendiente de re-medir el overscan del tubo.

## Comments

- **El "80 columnas" es inalcanzable con la medición actual:** 80×4=320 px
  llena el fb sin margen lateral, y el tubo recorta ~16 px a cada lado. Con la
  banda segura de 280 px, el máximo a 4 px es 70. Solo re-medir el overscan
  podría desbloquear 80.
- **Limitación aceptada:** esquinas y tees DEC (j–n, t–w) pierden ancho de
  brazo en el merge OR por pares (p.ej. esquina `j` con brazo vertical de 2 px);
  las cajas siguen conectando entre celdas contiguas. Documentado en ADR-0004.
- **El bug raíz de esta feature se diagnosticó después:** el teclado BLE "no
  respondía" porque el stack NimBLE contendía la radio 2.4GHz con el handshake
  SSH y libssh (SSH_TIMEOUT_INFINITE) colgaba el task en AUTHING, donde las
  teclas se descartan. Fix en el commit 876cfce (timeout + arranque diferido
  del BLE). No era una regresión de las 70 columnas: la ruta ssh/kbd/keymap es
  idéntica entre b59a402 y 6d77c03.