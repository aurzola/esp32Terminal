# AGENTS.md — ESP32 Terminal (CRT)

Terminal vintage sobre CRT B/N conectado a un **ESP32-S3**. Reutiliza el driver
de video **LCD_CAM+GDMA** (`src/video_s3.*`) de `lunarlander/esp32LanderS3`
(ver el AGENTS.md del juego para el detalle del driver).

## Reglas

- **Toda tarea de implementación termina SIEMPRE subiendo a la placa**
  (upload) sin preguntar: `bash flash.sh`. Solo se omite (avisando) si no hay
  placa, el build falla o el usuario pidió no subir.
- Idioma del código: inglés. Respuestas al usuario: español.
- No añadir comentarios al código salvo que se pidan.

## Tooling

- Placa / FQBN: **ESP32-S3** `esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app`
  (core `esp32:esp32` 3.3.10 / IDF 5.x — el driver GDMA y libssh lo exigen).
  `huge_app` (3MB app, sin OTA) es necesario desde M3: libssh eleva el binario
  por encima del límite de 1.2MB de la partición default.
- Puerto: `/dev/ttyACM0`.
- Compilar: `bash build.sh` · Subir: `bash flash.sh`
- Librerías: `lvgl@9.5.0` (arroject, Arduino), `NimBLE-Arduino`,
  `LibSSH-ESP32`.

## Estructura

| Archivo | Contenido |
|---------|-----------|
| `esp32Terminal.ino` | setup/loop: pm lock, video, LVGL (flush_cb → fbShadow), tick/timer |
| `src/video_s3.{h,cpp}` | **Copia** de `esp32LanderS3/src` (NTSC LCD_CAM+GDMA). No tiene sync automático; si cambia, decidir explícitamente si reflejarlo al juego |
| `src/lv_conf.h` | Config LVGL recortada (LV_COLOR_DEPTH 8; widget set mínimo) |
| `src/config.h` | Constantes del terminal (resolución hoy; WiFi/SSH en M3) |
| `build.sh` / `flash.sh` | Compile y upload (FQBN S3, puerto ACM0) |

## Notas de integración

- `video_wait_frame()` bloquea hasta el EOF del campo anterior y compone el
  campo nuevo; LVGL pinta durante el barrido del campo anterior (sin tearing).
- El flush de LVGL copia el área dirty a `fbShadow` (8-bit = 1 byte/píxel,
  memcpy directo; la `lut[]` del driver mapea 0–255 → código CRT).
- Quirks/contexto del driver (LCD_CAM regs, GDMA ring, timing NTSC): ver el
  AGENTS.md de `lunarlander` sección "Port a ESP32-S3".

## Agent skills

### Issue tracker

Issues and specs live as markdown files in `.scratch/`. See `docs/agents/issue-tracker.md`.

### Triage labels

Five canonical triage roles, each label string equal to its name. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout: one `CONTEXT.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
