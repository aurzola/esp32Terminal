# 04: Teclado BLE (HID host)

**What to build:** El terminal actúa como host HID (HOGP) de un teclado BLE fijo: se conecta a la MAC de `config.h`, se reconecta si se cae, y las teclas entran a la sesión activa. Hasta que exista SSH, se prueban con eco local en el grid (tecla → carácter en la sesión falsa).

**Blocked by:** 01

**Status:** resolved

- [x] Se conecta al teclado cuya MAC está en `config.h` al encender; se reconecta solo si se pierde.
- [x] Alfanuméricos, Enter, Backspace, flechas y teclas de función se mapean a ASCII/VT100 según el layout de `config.h`.
- [x] Las teclas entran a la sesión activa; sin sesión, eco local visible en el grid.
- [x] El estado del teclado (conectado/perdido) se refleja en el Estado del CRT.

## Comments

### Implementado (2026-08-30)

- BLE HID host (HOGP) en `src/kbd.h/.cpp`: se conecta a la MAC de
  `TERM_KBD_MAC` (config.h), se reconecta sola al caerse (poll con backoff de
  reintento), subscribe al report de HID y publica el estado a la línea de
  Estado del CRT (`termKbdState`). Pairing por passkey fijo (`TERM_KBD_PIN`)
  mostrado en la línea de Estado. El task de NimBLE solo encola eventos; el
  main loop los drena (poll()) para que las callbacks corran en su hilo.
- Seam de testing: `KeyMap::map` puro, testeado en host
  (`tests/keymap/keymap_test.cpp`). El transporte BLE no se testea en host.
- Estado de la issue marcado como resolved en la sesión de M3 (tickets 05/06):
  el código estaba commiteado pero la issue seguía en ready-for-agent.

### Extensión: auto-discovery para teclado con dirección rotatoria (2026-09-01)

El teclado de producción ("Bluetooth Keyboard", `13:05:aa:xx:xx:xx`) rota la
dirección en cada par de anuncios (solo cambian los últimos 3 octetos; el
prefijo `13:05:aa` es estable). Un MAC fijo nunca conecta. Por eso se añadió:

- `TERM_KBD_AUTO` + `TERM_KBD_PREFIX "13:05:aa"`: escanea en cada (re)intento,
  elige por nombre "keyboard" o por prefijo de MAC, y conecta a la dirección
  viva. `TERM_KBD_SCAN`=1 es el modo diagnóstico (logea `[kbd-scan]`).
- `TargetCallbacks` global `g_targetCallbacks` (un objeto local en stack
  provocaba `Guru Meditation LoadProhibited`); `discoverTarget(20)` con un
  `delay(250)` tras `scan->start` para no perder el adv tardío del host task.

Hallazgos de memoria (causa raíz de BLE_INIT: Malloc failed): con SSH activo el
task SSH pinnea un stack de 50KB de DRAM interna, dejando el mayor bloque
interno contiguo en ~7.6KB pese a ~137KB libres totales. El controller BLE
necesita un chunk contiguo para la conexión ACL y fallaba al asignar. Al bajar
`CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` a 155 y `MAX_CONNECTIONS` a 1 (en
`kbd.cpp` antes de incluir NimBLE) la discovery de servicios completa (HID
0x1812 presente) y el `Malloc failed` se reduce (aún aparece de forma
intermitente).

Bloqueo restante: el teclado, por ser device budget, se desconecta a mitad de
la discovery de características (`reason=531/520`, `lastErr=7`; a veces
`report=1 canNotify=0`, otras `services=3` sin HID). La secuencia actual
(connect + `getServices` síncronos en el main loop con el video pausado) no es
tolerada por el peer. Pendiente: mover el connect+discovery al path async del
host NimBLE para que las respuestas ATT lleguen a tiempo.

Fixes aplicados y commiteados:
- `28e2ee9` auto-discovery por prefijo/nombre.
- `02ecc1a` client único persistente reciclado (elimina `createClient failed`
  al rotar la dirección) + footprint NimBLE reducido (MTU 155, 1 conexión).

### Causa raíz del "no llegan teclas" + fix (2026-09-02)

El bloqueo real (el subscribe se verificaba pero las teclas no llegaban con el
teclado budget) era la **selección de la característica HID**:

- El teclado budget (`13:05:aa`) expone **varias** características `0x2a4d`
  (Report): input (props 0x12 READ|NOTIFY), output (0x1a READ|WRITE|NOTIFY),
  feature (0x0e WRITE|INDICATE sin NOTIFY), etc.
- El código viejo usaba `svc->getCharacteristic(0x2a4d)`, que en NimBLE
  devuelve **solo la última** descubierta: el callback de discovery
  (`characteristicDiscCB`) sobreescribe `m_pBuf` en cada match, así que con N
  reportes 0x2a4d queda la última (la feature, sin NOTIFY). `canNotify()` era
  false → `subscribeHid` no suscribía nada y el host nunca registraba el input
  report. Las teclas del budget jamás llegaban (silenciosamente).
- Fix: iterar `svc->getCharacteristics(true)` y elegir la primera chr con
  `canNotify() && canRead() && !canWrite()` (el input report), con fallback a
  cualquier chr notifiable. `NimBLERemoteService::subscribeHid` reescrito en
  `kbd.cpp`.

Otros fixes del mismo bloqueo:
- `kbdAddrType()`: clasificar como RANDOM solo si `(first & 0xc0) == 0xc0`, si
  no PUBLIC. El viejo `& 0xc0 != 0` clasificaba MAC públicas como `ac:`
  (M5Cardputer) como random → connect timeout `rc=13`.
- CCCD: el budget acepta el write del CCCD pero no lo refleja en un read
  posterior (falso negativo). Se confía en el write en vez de exigir verified=1.
- `client->connect(..., exchangeMTU=true)`.

Validado contra dos peers: el **M5Cardputer** (boot input 0x2a22, peer
determinista) y el **teclado budget** (múltiples 0x2a4d). Ambos entregan teclas
al grid. El Cardputer se usó como banco de pruebas (firmware
`cardputer/cardputer_kbd.ino`, muestra CONNECTED/ADVERTISING en pantalla);
conservar como herramienta de validación del host.

### Pendiente: BLE_INIT: Malloc failed con SSH activo + budget suelta el link (2026-09-02)

El fix de selección de chr está validado en **fake session** (`TERM_SSH_SESSION
0`): el budget conecta, subscribe y las teclas llegan al grid. **Con SSH activo
(`TERM_SSH_SESSION 1`) el teclado budget sigue sin funcionar** por DOS causas
que se combinan:

1. **`BLE_INIT: Malloc failed`** (tag de `nimble_port.c` / controller). Con el
   stack SSH de 50KB pinneado en DRAM interna, el controller BLE no encuentra
   un chunk contiguo para la conexión ACL y falla. Intermitente: a veces la
   discovery completa, a veces no. Ya se bajó `ATT_PREFERRED_MTU` a 155 y
   `MAX_CONNECTIONS` a 1 (commit `02ecc1a`), pero sigue apareciendo.
   En la sesión se probó `exchangeMTU=false` (src/kbd.cpp:517-518) y **no**
   eliminó el Malloc failed → no es el disparador.

2. **El teclado budget suelta el link (`onDisconnect reason=531`)** justo tras
   conectar/subscribir, en cada intento con WiFi activo. En fake session (sin
   WiFi) se mantenía conectado. Huele a contención de radio 2.4GHz
   WiFi/BLE/CRT-DMA: el peer budget es frágil y suelta ante retransmisiones
   perdidas. Patrón observado (una captura de 90s):
   - intento 1: `cccd=0, subscribe done=0` (se cayó antes del CCCD)
   - intento 2: se cayó sin llegar a listar services
   - intento 3: `cccd=1, subscribe done=1, connected + subscribed` → luego `531`
   - intento 4: `connect() failed rc=13` (timeout)
   Y un `BLE_INIT: Malloc failed` a los 54627ms durante el intento 3.

**Pendiente para la próxima sesión de diagnosing-bugs (Phase 1 = reconstruir el
bucle primero):**

- Reconstruir el bucle: `TERM_SSH_SESSION 1` + budget en pairing sostenido.
  Medir `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` **justo antes
  de `ensureRadio()`/connect** con SSH activo, para confirmar si hay bloque
  contiguo suficiente para la ACL del controller.
- Hipótesis a rankear (3-5): (a) Malloc failed = falta DRAM contigua (subir
  PSRAM para el pool NimBLE / bajar `ATT_PREFERRED_MTU` más / mover el init BLE
  antes del stack SSH); (b) 531 = contención de radio (pausar el WiFi o el DMA
  durante el connect BLE, o retrasar el arranque del teclado hasta que el SSH
  esté estable en SHELL — `startKbdIfReady` ya lo hace pero el SSH está en
  timeout de handshake en esta sesión); (c) el SSH del ESP32 da timeout
(`connect failed: Timeout connecting`) aunque el host `<host>:22`
   responde desde el PC → el teclado no arranca porque depende del estado SSH;
  aislar el SSH (¿el timeout es del TCP o del handshake SSH?).
- Reintentar el bucle en fake session como control sano: el budget funciona
  con `TERM_SSH_SESSION 0` — ese es el punto verde de referencia.

### Resuelto: Malloc failed / 531 era DRAM contigua, no contención (2026-09-02)

Causa raíz confirmada por instrumentación (`heap_caps_get_largest_free_block`
justo antes del connect, con SSH activo): el **controller BTDM consume ~63KB de
DRAM interna fijos en init**, y con el stack SSH de 51KB + WiFi + libssh ya
reservados solo quedaban **~7.6KB contiguos** antes del connect → el alloc ACL
del controller en el data-path post-connect fallaba (`BLE_INIT: Malloc failed`)
→ el GATT se estancaba → el teclado budget (frágil) cortaba el link
(`reason=531` REM_USER_CONN_TERM, y `520` supervision timeout). En fake
session (sin SSH) tras el init quedaban 118KB contiguos y el budget se
mantenía → contención de radio descartada como causa raíz (mismo BLE+CRT sin
WiFi y aguanta).

Fix (dos palancas sobre el presupuesto de DRAM interna):

1. **Inicializar el radio BLE al boot, antes de `ssh.begin()`**: nuevo
   `KbdHost::radioInit()` llamado en `setup()` antes del stack SSH. El
   controller carve su pool de un heap limpio (largest 139KB), en vez de de los
   restos que deja el SSH (~7.6KB). El connect del teclado sigue gateado al
   estado SSH (`startKbdIfReady`), así que no hay tráfico de radio durante el
   handshake. `esp32Terminal.ino`.
2. **`TERM_SSH_STACK_BYTES` 51200 → 32768**: el hwm del task SSH (~3KB en
   boot, crypto dentro) cabe holgado en 32KB; libera ~19KB más de DRAM
   contigua para el connect del teclado. `src/config.h`.

Resultado medido antes del connect: **internal=27KB, largest=17.4KB** (antes:
8.5KB / 6.1KB). El budget conecta, subscribe (`cccd=1`) y **se mantiene
conectado** con SSH activo y WiFi conectado; sin `Malloc failed` ni `531` en
capturas largas. La sesión SSH conecta y funciona con el teclado (validado en
placa); el `connect failed: Timeout connecting` que aparecía en sesiones
previas era transitorio (host/red), ya no se reproduce.