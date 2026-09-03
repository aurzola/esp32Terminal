# Spec: M1 — primer camino vertical (bytes → grid → CRT)

**Status:** ready-for-agent

## Problem Statement

El ESP32-S3 con CRT B/N todavía no es un terminal: hay video (LCD_CAM+GDMA) y
LVGL funcionando en la placa, pero no existe ningún camino que convierta bytes
de una sesión en texto visible. No hay grid de texto, no hay scroll, no hay
historial. Todo lo que llegue por la sesión debe acabar renderizado en el CRT
dentro del área segura que deja ver el overscan del tubo, sin romper ni el
driver de video ni la memoria de la placa.

## Solution

Implementar el primer camino vertical completo de la terminal: los bytes que
el host escribe por USB-serial entran en una **sesión falsa** que vierte a un
**grid** de texto puro (35×26 celdas de 8×8 px, dentro del área segura con
márgenes asimétricos) y ese grid se pinta en el CRT vía LVGL. El grid además
mantiene scroll de línea y un **scrollback** fijo (~4000 líneas) en PSRAM.
Es la base sobre la que después se apoya el parser VT100.

## User Stories

1. As a user, I want to see a few seed lines on the CRT right after boot, so that I know the path bytes→grid→CRT works without typing anything.
2. As a user, I want to send text over USB-serial and see it appear on the CRT, so that the fake session behaves like a real interactive line.
3. As a user, I want CR and LF to advance the cursor line-by-line, so that multi-line output reads naturally.
4. As a user, I want typing past the last column to wrap to the next line, so that long command output never overflows memory.
5. As a user, I want scrolling at the bottom of the grid to push lines into the scrollback, so that history beyond the visible area is preserved.
6. As a user, I want the scrollback to be a fixed-size ring in PSRAM, so that a long session neither grows unbounded nor steals the grid's heap.
7. As a user, I want every grid refresh to redraw only the dirty rows through the existing flush, so that there is no visible tearing on the CRT.
8. As a user, I want exactly one byte per pixel in the framebuffer handoff, so that the 8-bit LUT of the driver keeps the phosphor shades correct.
9. As a user, I want the safe-area margins to keep every glyph fully visible despite the tube's overscan, so that nothing is clipped on the left, right or top.
10. As a user, I want the fake session toggleable in configuration, independent of the terminal build, so that M1 can be verified on hardware and later replaced by the real SSH feed.
11. As a user, I want control bytes that M1 does not understand to be ignored without corrupting the grid, so that stray serial noise is harmless.
12. As a user, I want the grid logic to run without LVGL or hardware, so that it can be unit-tested on the host.
13. As a user, I want a session-start to begin with a clean grid and scrollback, so that a past session's content never leaks into the next one.
14. As a user, I want one LVGL label per visible row updated only when that row is dirty, so that rendering cost stays proportional to real changes.
15. As a developer, I want the unit test seam to be the public TermGrid interface, so that I can verify CR/LF/wrap/scroll semantics on the host without flashing the board.

## Implementation Decisions

- **Sesión falsa activable desde configuración** (`TERM_FAKE_SESSION`): cuando
  está activa, los bytes de USB-serial son el **feed** de la sesión. Es el
  sustituto temporal de la sesión SSH real (M3), y se desactiva igual de fácil
  en `config.h`.
- **Grid 35 columnas × 26 filas** a 8×8 px por celda, resultado de la medición
  de overscan del tubo (era 40×30 pre-medición). El área segura se materializa
  con **márgenes asimétricos**: 20 px arriba y a los lados, 12 px abajo;
  derivados de que el tubo recorta ~18-20 px arriba y ~16 px a los lados, y
  abajo llega completo. Solo se cambian si se re-mide el overscan.
- **TermGrid es un módulo puro** (sin LVGL, sin hardware): guarda el grid
  visible (`screen_`), un vector de filas sucias (`dirty_`), la posición del
  cursor y el ring de scrollback. Su interfaz pública es el **único seam de
  testing** de esta feature.
- **Semántica de putChar (M1, sin parser):** imprimibles `0x20`–`0x7e`,
  `CR` → columna 0, `LF` → `newline()`, resto de control ignorado. No hay
  todavía códigos VT100 (ticket 02).
- **Contrato de `newline()`:** baja el cursor; en la última fila desplaza el
  grid hacia arriba, empuja la línea superior al buffer de scrollback y
  **siempre reinicia el cursor a columna 0**. Este reset de columna es parte
  del contrato: el bug raíz de M1 (StoreProhibited en el boot) fue que un
  cursor de col 36+ desbordaba `screen_[r]` en BSS y corrompía el puntero del
  framebuffer global (0x3fcb3424 → 0x01cb3424), por lo que el primer flush de
  LVGL escribía a una dirección inválida.
- **Scrollback:** ring fijo de `TERM_SCROLLBACK_LINES` (4000) × 35 bytes en
  PSRAM (`MALLOC_CAP_SPIRAM`), reservado al iniciar la sesión y descartable
  sin romper el boot si la PSRAM no está disponible. Se limpia al iniciar
  sesión.
- **Render por filas:** una etiqueta LVGL por fila visible; `set_text` solo
  cuando la fila está marcada sucia tras el feed. El flush de LVGL copia el
  área sucia a `fbShadow` (1 byte/píxel, `memcpy` directo; la LUT del driver
  mapea 0–255 → código CRT) trasladando las coordenadas por los márgenes del
  área segura.
- **El driver de video no se toca** (copia verbatim de esp32LanderV3): toda la
  geometría del área segura se resuelve en la capa de aplicación, no en el
  driver.

## Testing Decisions

- **Un único seam:** la interfaz pública de `TermGrid` (`init`, `feed`,
  `rowText`, `isRowDirty`/`clearRowDirty`, `cursorRow`/`cursorCol`,
  `scrollbackLines`). Se compila `term.cpp` + la configuración en el host
  (sin LVGL, sin hardware) y se verifica el estado observable del grid.
- **Qué hace bueno un test:** alimentar bytes a `feed()` y comprobar el estado
  externo resultante — contenido de cada fila (`rowText`), líneas de
  scrollback, posición del cursor, filas sucias — nunca internals del módulo.
- **Casos mínimos a cubrir:** seed multi-línea → filas correctas; CR/LF;
  wrap en col 35 (líneas de 36+ chars no desbordan y pasan de fila); scroll en
  la fila 26 empuja al ring; BT no rompe nada; `TERM_SCROLLBACK_LINES` se
  comporta como ring (no crece sin límite); cursor en 0 tras newline.
- **Prior art:** no hay aún infraestructura de tests en host en este repo
  (es Arduino); este spec la introduce por primera vez. El harness on-device
  `tests/video_test/` y la alfombra `TERM_LVGL_TEST` quedan como verificación
  visual de respaldo, fuera del contrato de testing.

## Out of Scope

- Parser VT100 (ticket 02): posicionamiento de cursor, borrados, SGR.
- Apps de pantalla completa (ticket 03).
- Teclado BLE (ticket 04).
- Sesión SSH real (ticket 05).
- Fósforo / decay phosphor (ticket 06).
- UTF-8: la sesión es un stream ASCII/VT100 7-bit (ADR-0003).
- Menú, multi-host y shell local (ADR-0001).

## Further Notes

- M1 ya está implementado y en marcha en la placa: compila, flashea, el seed
  se muestra y el scrollback aguanta cientos de líneas sin crash.
- El bug raíz (StoreProhibited) quedó documentado como justificación del
  contrato de `newline()`: reiniciar el cursor es obligatorio, no un detalle.
- El cambio de geometría (márgenes/celdas) es una frontera que solo se cruza
  tras re-medir el overscan del tubo.