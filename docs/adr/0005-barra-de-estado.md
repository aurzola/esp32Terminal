# 0005 Barra de estado en la fila 27

La línea de estado de abajo deja de ser texto ad-hoc pintado por el `.ino` y
pasa a ser una **barra de estado** de primera clase, con contrato propio
(documentado en el ticket 10).

## Decisiones

- **La barra es la fila 27 del grid**, la última de `TERM_ROWS=27`. La sesión
  vive en las filas 1..26 (región de scroll `\x1b[1;26r`), así que la barra
  nunca se scrolla. Se eligió mantenerla **como fila del grid** (Q2) frente a
  una banda dedicada aparte: reutiliza parser, render y geometría, y el look de
  barra se obtiene pintándola con el atributo **inverso** (fondo claro, texto
  oscuro). No hay capa de render propia que casar con el fósforo.
- **Geometría: 27 filas es el techo.** 27×8px = 216px + 20px top = 236 ≤ 240
  (margen inferior 4px). 28 filas exigen 224px y margen top ≤16px, por debajo
  del recorte real del tubo (~18px arriba). Se verificó en placa que la fila 27
  se ve completa. Más filas requieren re-medir el overscan superior.
- **Contenido en tres segmentos** (56 cols): izq = estado SSH (glifo +
  etiqueta corta), centro = teclado (`kbd` / `kbd lost`), der = `DD-MON HH:MM`
  (24h). Los **mensajes transitorios** — prompt de password (AUTH), PIN de
  pairing, debug USB — **secuestran la barra entera** mientras duran (Q4).
- **Iconos como glifos propios.** El stream de la sesión sigue siendo ASCII
  7-bit (ADR-0003); los iconos (`⌁ ⏻ ⌑ ✓ ↻ — ⌨ ✕`) son glifos bitmap añadidos a
  la tabla (pipeline de `decGlyphCells`) y pintados solo por la barra. No pasa
  nada por el parser.
- **Prioridad de transitorios (Q11):** (1) las acciones requeridas (prompt de
  password, PIN de pairing) secuestran la barra hasta resolverse; (2) entre
  dos, gana el PIN de pairing (sin teclado no se puede tipear el password);
  (3) el debug USB se muestra cuando no hay acción requerida; (4) la barra
  normal se recompone al resolverse el transitorio. Esto arregla el bug de que
  `termKbdPair()` pintaba el PIN y un `termStatusDraw()` posterior lo pisaba.
- **Hora por NTP (Q6):** no hay RTC; el WiFi ya existe para SSH, así que se
  sincroniza con `esp_sntp` cuando el task de SSH entra en SHELL. La zona es
  una constante `TERM_TZ` (string POSIX) en `config.h` aplicada con
  `settimeofday`. Sin NTP → `--:--`.
- **Repintado mínimamente invasivo (Q7):** la barra se redibuja solo cuando
  cambia su contenido: el estado al cambiar, la hora al cambiar de minuto
  (chequeo de 1s en `loop()`). No se exime del decay P39: la barra sufre el
  mismo ghost que el resto de la imagen, coherente con el CRT.

## Consecuencias

- Nuevo módulo `src/status.{h,cpp}`: la composición de segmentos, el truncado
  a 56 cols y la prioridad de transitorios son lógica pura testeable en host
  (`tests/status/`), como `passprompt`. Los glifos de barra son una tabla
  estática dentro de `status.cpp`. El `.ino` deja de tener `termStatusLine`/
  `termStatusDraw`; `ssh.cpp` aporta un `stateText()` compacto.
- `TERM_ROWS` pasa a 27 (margen inferior 4px).
- Un lector futuro que mueva la barra fuera del grid (banda fina) estará
  cruzando la decisión Q2: ganaría una fila de texto para la sesión pero crea
  una capa de render propia que hay que casar con el fósforo.