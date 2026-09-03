# ESP32 Terminal

Terminal vintage sobre CRT B/N (320x240) en un ESP32-S3 con video compuesto
por **LCD_CAM + GDMA**. Capa gráfica: **LVGL** (8-bit grayscale). Teclado
**BLE** (host HID / HOGP con NimBLE) y conexión **SSH** a un host
(`LibSSH-ESP32`).

## Funcionalidad

- Sesión SSH interactiva full-screen (libssh + pty) con prompt local de
  password enmascarado en el CRT y reconexión automática con backoff.
- Teclado BLE HID host con auto-discovery (para direcciones rotatorias),
  layout español ISO con AltGr, o USB HID boot protocol.
- Barra de estado (fila 27) con hora NTP, iconos de WiFi/SSH/teclado y
  mensajes transitorios (PIN de pairing, prompt de password).
- Fósforo tipo P39 (persistencia del CRT) y scrollback de 4000 líneas en PSRAM.

## Estado

| Hito | Descripción | Estado |
|------|-------------|--------|
| M0 | Video (LCD_CAM+GDMA) + LVGL en CRT | Hecho |
| M1 | Terminal rendering: parser VT100 + widget LVGL | Hecho |
| M2 | Teclado BLE (HOGP host) | Hecho |
| M3 | SSH interactivo (libssh + pty) | Hecho |
| M4 | Hardening: RAM a PSRAM, reconexión, fósforo | Hecho |

## Configuración

Los valores de WiFi/SSH (SSID, password, host, usuario) son **placeholders** en
`src/config.h`. El password de SSH se teclea en el CRT en cada conexión
(ADR-0002), o se puede fijar en `TERM_SSH_PWD` para auto-login.

## Proveniencia de los fuentes

- `src/video_s3.{h,cpp}` — **copia verbatim** del driver NTSC LCD_CAM+GDMA
  (bus GPIO4/5/6/7/15/16/40/41) del proyecto `lunarlander` / `esp32LanderS3`.
  Si evoluciona aquí, decidir explícitamente si se refleja al juego; no hay
  sync automático.
- `src/lv_conf.h` — desde `lv_conf_template.h` de LVGL 9.5.0, recortado
  (widgets que la terminal no usa fuera, `LV_COLOR_DEPTH 8`).

## Dependencias

- Core: `esp32:esp32` ≥ 3.3.10 (IDF 5.x, requerido por el driver GDMA).
- Librerías Arduino: `lvgl@9.5.0`, `NimBLE-Arduino`, `LibSSH-ESP32`.

## Build / flash

```sh
bash build.sh                    # compila
bash flash.sh                    # sube a /dev/ttyACM0
```

FQBN: `esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app` (huge_app
necesaria desde M3 por el tamaño del binario con libssh).

## Arquitectura (resumen)

```
loopTask (core 1):  video_wait_frame() → lv_timer_handler() → flush ⇒ fbShadow
sshTask   (core 0): WiFi + libssh (pty) ; rx→term, tx desde teclado
teclado BLE:        NimBLE host (HOGP) → eventos → cola → loopTask
```

Grid de terminal: 56 cols × 27 filas (celdas 5×8 px), scrollback en PSRAM.