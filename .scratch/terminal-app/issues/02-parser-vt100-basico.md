# 02: Parser VT100 básico

**What to build:** Sobre el feed de bytes, el widget de sesión responde a los códigos VT100 del nivel "minimal CLI": posicionamiento de cursor, borrados y atributos (bold → rampa alta de gris, inverse). Con esto un `ls` y un prompt se ven bien en el CRT.

**Blocked by:** 01

**Status:** resolved

- [x] Cursor: home (`ESC[H`), CUP (`ESC[r;cH`), movimientos lineales (arriba/abajo/izq/der) y forward/back.
- [x] Borrados: pantalla (`ESC[2J`), línea, y desde el cursor; los espacios borrados también desaparecen del scrollback.
- [x] SGR: bold → gris alto en la rampa; inverse → fondo claro / texto oscuro.
- [x] Tras conjuntar clear + home, el grid queda limpio completo.
- [x] Los bytes de control no consumidos pasan de largo sin romper el grid.

## Comments

### Implementado (2026-08-29)

Parser VT100 en `TermGrid::feed` (máquina de estados miembro 0/1/2/3:
texto, ESC, CSI, OSC) + `execCsi`. CUP/movimientos con clamp, J (modos
0/1/2; 2J limpia también el scrollback), K (0/1/2) con eraseRange que
resetea attr por celda, SGR m/22/27 con attr por celda. OSC (`ESC]..BEL`)
y ESC+letra se consumen y descartan; el estado persiste entre feeds
(streaming USB-serial por bloques de 64B).

Render: se eliminan los labels LVGL por fila; el texto se rasteriza
directo a `fbShadow` desde los bitmaps de `lv_font_unscii_8` (A1→A8 vía
`lv_font_get_glyph_bitmap`), con `rowAttr` por celda → niveles de rampa
(`TERM_COLOR_*` en config.h): inverse = fondo 255/texto 0, bold = 255,
normal = 190. El raster de filas dirty se hace tras `lv_timer_handler`
(no compite con el fondo de LVGL). Upload omitido: no había placa en
/dev/ttyACM0 (build OK, host tests OK).