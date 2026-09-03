# 05: SSH real + vida completa

**What to build:** El terminal de verdad: WiFi desde `config.h`, conexión libssh al host, prompt local de password enmascarado por el propio teclado, y la sesión SSH a pantalla completa. Cuando el WiFi o el host caen, el Estado muestra `disconnected — retrying…` y el terminal reconecta solo con backoff infinito.

**Blocked by:** 03, 04

**Status:** resolved

- [x] Arranque: WiFi desde config → conexión al host → prompt local de password (asteriscos) → sesión SSH full-screen.
- [x] El password se muestra como `***` y no entra a la sesión hasta Enter.
- [x] En caída de WiFi/host: Estado `disconnected — retrying…` en el grid y reconexión automática con backoff, sin intervención.
- [x] El stream del host alimenta el mismo widget/parser que el feed sintético (02/03 sin cambios).

## Comments

### Implementado (2026-08-30)

- **Módulo `src/ssh.h/.cpp`** (hardware-bound, como kbd.cpp): WiFi desde
  `config.h` + sesión libssh en un task propio (libssh bloquea; el loop de
  video sigue corriendo). Estados: WIFI → CONNECT → AUTH (prompt local) →
  AUTHING → SHELL → RETRY. Password con re-prompt en la misma conexión si es
  erróneo; caída → RETRY con backoff exponencial (sshBackoffDelayMs) y
  reconexión infinita sin intervención.
- **Seams puros (host, testeados):** `PassPrompt` (`src/passprompt.h/.cpp`,
  password enmascarado con backspace/commit) y `sshBackoffDelayMs` →
  `tests/passprompt/passprompt_test.cpp`.
- **Comunicación con el loop:** el task SSH lee el canal con timeout y vuelca
  bytes a un stream buffer (rx); el main loop los drena a `grid.feed()` (mismo
  parser que el feed sintético). El teclado (seam KeyMap) se enruta a la
  sesión en SHELL, al PassPrompt en AUTH, y se ignora mientras conecta.
- **Estado en el CRT:** la última fila del grid (fila de Estado) muestra el
  estado de la sesión (`wifi connecting...`, `password: ***`, `disconnected -
  retrying...`) combinado con el estado del teclado.
- **Config:** `TERM_SSH_SESSION` (1 = SSH real, 0 = fake session),
  `TERM_WIFI_SSID/PASS`, `TERM_SSH_HOST/PORT/USER`. Credenciales en
  placeholders; el dueño las rellena en `config.h` (ADR-0001/0002).
- **Flash:** libssh eleva el binario a ~1.44MB, por encima de la partición
  default (1.2MB). Se cambia el FQBN a `PartitionScheme=huge_app` (3MB app,
  sin OTA) en build.sh/flash.sh y se documenta en AGENTS.md.
- **Upload:** subido a la placa vía `bash flash.sh` (45% flash / 74% RAM). Con
  credenciales placeholder, el terminal arranca en Estado de retry, que es el
  comportamiento esperado hasta configurar la red/host.