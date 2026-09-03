# ESP32 Terminal

Terminal vintage CRT por SSH en ESP32-S3: sesión SSH interactiva a un host, renderizada full-screen en un CRT B/N 320×240 vía LCD_CAM+GDMA y LVGL.

## Language

**Host**:
La máquina remota a la que el terminal se conecta por SSH.
_Avoid_: servidor, máquina

**Sesión**:
Stream de texto interactivo entre un host y el terminal, mostrado a pantalla completa en el CRT.
_Avoid_: ventana, pestaña

**Grid**:
El reparto de la pantalla en celdas de carácter: 56 columnas × 27 filas a 5×8 px, dentro del área segura de 320×240 que deja ver el overscan del tubo. Las 26 primeras filas son la sesión (región de scroll `1;26`); la fila 27 es la **barra de estado**. La fuente es bitmap 5×7 colapsada a 4 px (`collapseColumns`) con 1 px de canalón derecho; el line-drawing DEC ocupa la celda completa. Más columnas o filas solo si se re-mide el overscan (ADR-0004).
_Avoid_: resolución de texto, layout

**Scrollback**:
El historial de una sesión más allá de las 30 filas visibles, guardado en PSRAM.
_Avoid_: memoria de texto, buffer

**Estado**:
La fila 27 (fija, fuera de la región de scroll) como **barra de estado** en
inverso: izq = sesión SSH (glifo + etiqueta corta), centro = teclado, der =
fecha/hora NTP. Los mensajes transitorios (prompt de password, PIN de pairing,
debug USB) secuestran la barra mientras duran (ticket 10 / ADR-0005).
_Avoid_: pantalla de carga, splash, banner

**Teclado**:
La única vía de entrada: un teclado BLE conectado por HID (host HOGP).
_Avoid_: ratón, táctil, puntero