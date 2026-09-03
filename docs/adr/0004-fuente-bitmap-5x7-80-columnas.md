# 0004 Fuente bitmap 5×7 clásica, camino a 56 columnas

El render usa una **fuente bitmap propia de 5×7** (`src/font5x7.h`, dominio
público, glcdfont de Adafruit GFX) en vez de una fuente LVGL. Los glifos ASCII
0x20–0x7F se cargan en `glyphCells[]` como celdas de 8 filas (glifo de 4 px,
colapso 5→4) y las cajas de line-drawing usan el ancho completo de la celda
(`decGlyphCells`), así que **una columna tiene hoy 5 px**: 56 columnas × 26
filas en el área segura.

Se eligió la fuente propia estrecha frente a `lv_font_unscii_8` (8×8) porque,
a 8 px por columna, el CRT se lee "muy grande" y el techo físico es 40
columnas (320 px / 8 px). Una fuente de 5 px permite columnas de 4 px (70
columnas, medida inicial) pero las celdas sin canalón hacían que las letras se
fundieran en el fósforo del tubo. Se pasó a **columnas de 5 px**: el glifo se
colapsa a 4 px y deja **1 px de canalón a la derecha** que separa los
caracteres, y **56 columnas caben exactamente en la banda segura de 280 px**
(márgenes 20/20, idénticos al layout de 35×8). El "80 columnas" original es
inalcanzable con la medición de overscan actual (~16 px recortados a cada
lado): para más columnas haría falta re-medir el tubo.

## Cómo se encoge el glifo

El glifo de 5 px no cabe con holgura en una celda de 4 px. `collapseColumns()`
(`src/glyph.h`) colapsa columnas contiguas con OR: `dst[s*cell_w/box_w] |=
src[s]` — para 5→4 funde `[0|1, 2, 3, 4]` (en la mayoría de glifos col0 está
vacía y equivale a pegarlos a la izquierda; cuando tiene tinta — `#`, `$`,
`%` — el OR la conserva), y el resultado se sitúa en los 4 px izquierdos de la
celda de 5 px dejando el bit derecho a 0 (el canalón). Para el **line-drawing
DEC**, el colapso es **8→5** `(0|1,2|3,4,5|6,7)` y ocupa los 5 px: las cajas
deben conectar entre celdas contiguas, así que no llevan canalón. Regla única,
determinista y testeable en host (`tests/term/term_test.cpp`); ningún glifo
pierde tinta, solo se aprieta. El render no asume un em-width concreto:
`buildGlyphs` y `layoutGlyphBits` reciben `cell_w` (`TERM_CELL_W`).

Un lector futuro que "arregle" el render sustituyendo la tabla por una fuente
LVGL proporcional estará rompiendo la rejilla de ancho fijo y la capacidad de
escalar la columna a 4 px, que es justo lo que esta decisión preserva.