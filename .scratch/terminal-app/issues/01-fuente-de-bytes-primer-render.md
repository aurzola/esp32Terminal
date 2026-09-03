# 01: Fuente de bytes + primer render

**What to build:** Lo que pones por USB-serial (desde el PC) es una "sesión falsa" de bytes que entra al widget de la sesión y aparece en el CRT: caracteres ASCII en el grid 35×26 (área segura medida tras overscan; era 40×30 pre-medición) con fuente 8×8, scroll de línea y scrollback guardado en PSRAM. Es el primer camino vertical completo: bytes → buffer de texto → widget LVGL custom → CRT, sin parser todavía.

**Blocked by:** None (can start immediately)

**Status:** ready-for-agent

- [x] La sesión falsa activable por `config.h`: los bytes recibidos por USB-serial son el feed del widget.
- [x] Caracteres ASCII visibles en el CRT en el grid 35×26 con la fuente de 8×8.
- [x] CR/LF producen scroll de línea; lo que sale por arriba queda en el scrollback.
- [x] Scrollback fijo (~4000 líneas) reservado en PSRAM; se limpia al empezar o perder la sesión.
- [x] El widget pinta solo el área sucia usando el flush existente (sin tearing perceptible).

## Comments

- **Bug raíz resuelto (StoreProhibited en boot):** `putChar` desbordaba
  `screen_[r]` cuando `newline()` no reseteaba `curC_` (líneas de 36+ chars).
  El cursor corría a col ≥36 y el overflow pisaba la variable global del
  framebuffer (0x3fcb3424 → 0x01cb3424), así que el primer flush de LVGL
  escribía fuera de rango → `Guru Meditation StoreProhibited` en bucle.
  Fix: `newline()` siempre reinicia `curC_ = 0` (ambas ramas). Verificado en
  placa: seed visible, 120+ líneas de scrollback sin crash.
- **Verificación host (seam: interfaz pública de `TermGrid`):** nuevo
  `tests/term/term_test.cpp`, compila sin Arduino/LVGL/hardware con
  `g++ -std=c++17 -I src -o /tmp/term_test tests/term/term_test.cpp src/term.cpp`
  (y variante ASAN). Cubre: wrap de línea larga (regresión del bug), CR/LF,
  reset de col en `newline()`, controles ignorados, scroll→ring, límite del
  ring, dirty-rows, seed de arranque. Todo verde.