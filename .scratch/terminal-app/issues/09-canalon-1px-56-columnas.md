# 09 — Canalón de 1 px entre caracteres (56 columnas a 5×8 px)

**What to build:** Los caracteres en el CRT se ven ligeramente ilegibles: a
celdas de 4 px sin canalón, las letras se funden en el fósforo del tubo (Q1-C:
se tocan Y las M/W anchas pierden forma). El grid pasa a **56 columnas × 26
filas a celdas de 5×8 px**: el glifo de texto se colapsa 5→4 y ocupa los 4 px
izquierdos de la celda, dejando **1 px de canalón a la derecha**; el
line-drawing DEC ocupa los 5 px completos para que las cajas conecten.

**Blocked by:** 08

**Status:** ready-for-agent

- [x] `TERM_COLS 56`, `TERM_CELL_W 5` en `config.h` (alto sigue 8); márgenes
      derivados: banda 280 px, margen X 20, margen B 12 (sin cambio de valor).
- [x] `buildGlyphs()`: texto colapsa 5→4 y `layoutGlyphBits(box_w=4, ofs 0,
      cell_w=5)` → bit de canalón (0x08) siempre a 0; DEC colapsa 8→5 y
      `layoutGlyphBits(box_w=5, cell_w=5)` → ancho completo.
- [x] `renderRowPixels()` itera `x < TERM_CELL_W` (5): en texto el bit 4 está
      a 0 (fondo = canalón), en DEC puede estar a 1 (línea a ancho completo).
- [x] Seed de fake-session a 56 dígitos.
- [x] Tests host: colapso 5→4 conserva tinta M/W y deja el bit de canalón a 0;
      colapso 8→5 mantiene la línea horizontal DEC a ancho completo y usa el
      canalón; seed 56 col. Verde (incl. ASAN).

## Comments

- **El canalón sale de la celda, no del glifo:** el glifo sigue a 4 px (la
  misma forma del ticket 08); el 5º px de cada celda es el aire. Es la decisión
  Q2-B del grill: 56 columnas (glifo 4 px + canalón) frente a 46 (glifo 5 px
  original + canalón). Si la M/W siguen feas en el CRT, abrir un ticket para un
  colapso 5→4 "inteligente" (descartar la columna con menos tinta por glifo).
- **El pty size anunciado al host es 56×25** (`ssh_channel_request_pty_size`
  usa `TERM_COLS`); las apps SSH se adaptan solas.
- **ADR-0004 y CONTEXT.md actualizados**: 56 columnas a 5 px, "80" sigue como
  frontera pendiente de re-medir el overscan del tubo.