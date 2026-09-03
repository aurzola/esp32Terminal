# 10 — Barra de estado (fila 27: iconos de estado + fecha/hora)

**What to build:** La fila de estado de abajo se convierte en una **barra de
estado** de primera clase (grill confirmado). Ocupa la **fila 27** del grid
(la última; la sesión vive en las filas 1..26 vía región de scroll) con
**estilo inverso** (fondo claro, texto oscuro). Muestra tres segmentos:
izq = estado SSH (glifo + etiqueta corta), centro = teclado (glifo + `kbd` /
`kbd lost`), der = fecha/hora `DD-MON HH:MM` (24h, NTP, zona `TERM_TZ`).
Los mensajes transitorios (prompt de password, PIN de pairing, debug USB)
**secuestran la barra entera** mientras duran.

**Blocked by:** 09

**Status:** ready-for-agent

## Grill cerrado (Q1–Q13)

- **Q1** (a) feature de primera clase: ticket + ADR + módulo.
- **Q2** (a) sigue siendo **una fila del grid**, reservada por la región de
  scroll (`\x1b[1;26r`, filas 1..26); la barra no se scrolla nunca.
- **Geometría:** empírico en placa con `TERM_ROWS=27` — la fila 27 se ve
  completa con 4px de margen inferior. **28 filas no caben** (224px + 20 top
  > 240): solo una re-medida del overscan superior desbloquearía más.
- **Q3** (a) fila 27 con fondo **inverso** (look de barra sin duplicar render).
- **Q4** (a) 3 segmentos fijos + transitorios a pantalla completa.
- **Q5** (c) glifo + etiqueta de 2–3 letras; set mínimo de glifos nuevos en la
  tabla (mismo pipeline que los DEC, 5px). El stream sigue siendo ASCII 7-bit
  (ADR-0003): los glifos son render de la app, la sesión no los envía.
- **Q6** (a) hora por **NTP** sobre el WiFi ya existente (`esp_sntp`,
  sincronizar cuando SSH entra en SHELL); fallback `--:--`.
- **Q7** (a) repintado **solo al cambiar el contenido** (estado → inmediato;
  hora → al cambiar de minuto, chequeo 1s en `loop()`); la barra sufre el
  mismo decay P39 que el resto (sin exención).
- **Q9** (a) nuevo módulo **`src/status.{h,cpp}`**: composición de segmentos,
  truncado y elisión como lógica pura testeable en host (como `passprompt`);
  glifos de barra como tabla estática dentro del propio `status.cpp`.
  `tests/status/` en host.
- **Q10** tabla estados → (glifo, etiqueta):
  `wifi.` `⌁` · `connect.` `⏻` · `auth.` `⌑` · `conect` `✓` · `retry.` `↻` ·
  `off` `—` · `kbd` `⌨` · `kbd lost` `⌨✕`. Las etiquetas usan punto ASCII
  (`wifi.`) en vez de `…`: el grid solo renderiza 0x20..0x7e.
- **Q11** prioridad de transitorios: (1) acciones requeridas (password prompt,
  PIN de pairing) secuestran hasta resolverse; (2) entre dos, gana el PIN de
  pairing; (3) USB debug cuando no hay acción requerida; (4) la barra normal se
  recompone al resolverse el transitorio. Fijar el bug de que `termKbdPair()`
  pinta el PIN y un `termStatusDraw()` posterior lo pisa.
- **Q12** `TERM_TZ` (string POSIX) en `config.h`, aplicado con `settimeofday`
  al sincronizar; formato `DD-MON HH:MM` 24h.

## Qué hacer

- [x] `config.h`: `TERM_ROWS 27` (margen inferior 4px), `TERM_TZ`; comentario
      de márgenes actualizado.
- [x] `src/status.{h,cpp}`: composición pura de la barra (segmentos, truncado a
      56 cols, elisión), prioridad de transitorios, y `draw()` que escribe la
      fila 27 vía `feed` + render inmediato (reemplaza `termStatusLine`/
      `termStatusDraw` del `.ino`).
- [x] Tabla de glifos de barra en `status.cpp` (mismo pipeline que `decGlyphCells`).
- [x] NTP en el task de SSH (sync al entrar en SHELL) + lectura de hora con TZ.
- [x] Ticker de minuto en `loop()` (redibujo solo si cambió el minuto).
- [x] `tests/status/status_test.cpp` en host (build.sh): composición de
      segmentos, truncado/elisión, prioridad de transitorios. Verde.
- [x] Comprobar en placa: barra inversa en fila 27, iconos, hora real, y que
      auth/pairing siguen secuestrando la barra.

## Comments

- **Por qué no fila 28:** 28×8px = 224px + 20px top = 244px > 240px del área
  segura; el tubo recorta ~18px arriba, así que el margen top no puede bajar a
  ≤16px sin recortar la fila 1. Fila 27 = máximo hoy.
- **El bug del PIN pisado** (Q11-2) es una motivación clave del módulo: la
  prioridad debe vivir en un solo sitio testeable.
- **ADR-0005 y CONTEXT.md** actualizados al implementar.