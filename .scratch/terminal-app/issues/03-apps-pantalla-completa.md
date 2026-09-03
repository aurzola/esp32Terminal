# 03: Apps de pantalla completa

**What to build:** El widget interpreta el conjunto "full-screen apps": scroll regions, alternate screen y el juego de line-drawing VT100 (`ESC(0` ↔ `ESC(B`). Con el feed sintético, `vim`/`top`/`nano`/`mc` se comportan como en un terminal de verdad.

**Blocked by:** 02

**Status:** resolved

- [x] Scroll regions (`CSI r`): el scroll y el cursor respetan la región definida.
- [x] Alternate screen (`CSI ?1049h/l`): buffer propio; al salir se restaura la pantalla principal sin residuo.
- [x] `ESC(0` pinta los carácter de line-drawing (cajas `┌─┐`…); `ESC(B` vuelve a ASCII.
- [x] La fuente 8×8 incluye los glyphs de line-drawing; cualquier glifo ausente pinta un fallback estable (espacio) sin romper el grid.

## Comments

### Implementado (2026-08-29)

- **Scroll regions:** `CSI r` (params ausentes o región inválida → pantalla
  completa). `newline()` en el borde inferior de la región desplaza solo las
  filas de la región — no toca las de fuera — y empuja la línea superior de la
  región al scrollback. El cursor vertical (CUU/CUD) queda clampado a la
  región; CUP conserva el posicionamiento absoluto (sin origin mode en este
  ticket).
- **Alternate screen:** `CSI ?1049h/l` (parámetro privado `?` en el parser).
  `term.h` refactorizado a `Cellbuf` (text/attr/dirty) con puntero `cur_` a
  `main_` o `alt_`: la pantalla principal queda congelada intacta, el buffer
  alterno se limpia y el cursor/attr se guardan; al salir se restaura exacto.
  El scroll dentro de la pantalla alterna **no** toca el scrollback de la
  principal, y `2J` en alt no lo borra (guard `inAlt()`).
- **Line-drawing:** bit `TERM_ATTR_ALTCHARS` como atributo por celda; `ESC(0`
  / `ESC(B` (estado 4 del parser, sobrevive feeds de 64B). El render usa una
  tabla propia de glyphs DEC 8×8 (`decGlyphBits`, 0x60–0x7e; glifos ausentes →
  espacio). ADR-0003: stream 7-bit, sin UTF-8.
- **Seam de testing (host):** `tests/term/term_test.cpp` ampliado — región
  (scroll solo en la región, reset, clamp A/B), alt screen (restaura sin
  residuo, scrollback preservado, 2J en alt), line-drawing (attr ALT, split
  de feeds, erase de célula). Todo verde, también bajo ASAN/UBSan.
- **Build:** `bash build.sh` OK (35% flash). Flash subido a placa vía
  `bash flash.sh` (RAM 85%, +~1.9KB por el buffer alterno).

### Code review (2026-08-29)

Ajustes tras `/code-review` (ejes Standards + Spec):

- Mapa DEC corregido a los códigos scan de vt100.net (`q`=─ horizontal,
  `x`=│ vertical, `t`/`u`/`v`/`w`=tees, esquinas `l`/`k`/`m`/`j`, `┼`=`n`):
  antes las tees y la vertical estaban desplazadas un código, y `≤`/`≥`
  (`y`/`z`) eran bitmaps idénticos. Se añaden π/≠/£/· (`{|}~`).
- `enterAlt` sin `memcpy` muerto del buffer principal (solo switch + erase);
  `resetRegion()` extrae la duplicación init/clear/`CSI r` inválido; borde de
  `newline()` con cursor debajo de la región en la última fila física ahora
  scrollea la región en vez de quedarse pegado.
- `TERM_ATTR_ALT` → `TERM_ATTR_ALTCHARS` para no chocar con `alt_` (buffer
  alterno). Comentarios añadidos recortados a estilo terse salvo cabecera de
  feed y etiquetas scan de la tabla.