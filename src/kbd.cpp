#include "kbd.h"
#include "config.h"

#include <string.h>

#include <string>

#include <Arduino.h>
// Reduce the NimBLE buffer footprint before the library headers are processed:
// one client connection is all the keyboard needs, and HID reports are tiny, so
// a small ATT MTU (with its per-connection mbuf) is plenty. On this board the
// BT controller competes for internal DRAM with the NTSC video + the 50KB SSH
// task stack; trimming these cuts the contiguous-buffer allocs that previously
// failed (BLE_INIT: Malloc failed).
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 1
#define CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU 155
#include <NimBLEDevice.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// The NTSC video driver (GDMA + LCD_CAM). Pausing it frees the BLE controller
// from radio starvation during latency-sensitive link work (connect + GATT
// discovery), which otherwise makes the discovery time out / crash.
extern "C" void video_pause(void);
extern "C" void video_resume(void);

// HID over GATT (HOGP) constants.
static const uint16_t kHidService = 0x1812;
static const uint16_t kReportChr = 0x2a4d;    // Report (HOGP keyboards)
static const uint16_t kBootInputChr = 0x2a22; // Boot Keyboard Input Report

struct KbdEvent {
    uint8_t type; // 0 = key press, 1 = state change, 2 = pairing pin
    uint8_t keycode;
    uint8_t mods;
    bool connected;
    uint32_t pin;
};

static KbdHost *g_host = nullptr;

// Pick the address type from the configured MAC: random only when the peer's
// first octet has its two top bits set (static or private random address,
// 0x40..0xFF); public otherwise (0x00..0x3F). The old bit-0x02 test misread
// public addresses like 0x13 as random, making connect time out.
// Behavioral peers (the budget `13:05:aa` keyboard, the M5Cardputer) all
// advertise with a PUBLIC address (type 0) regardless of their leading octet.
// The only addresses that are actually RANDOM are non-resolvable/private ones
// whose two top bits are both set (0xC0 mask). Test exactly that; a mask like
// `& 0xC0 != 0` wrongly classifies public first octets (0x40..0xBF, e.g. 0xac)
// as random and connect times out.
static uint8_t kbdAddrType(const char *mac)
{
    if (mac != nullptr && mac[0] != '\0') {
        unsigned first = 0;
        if (sscanf(mac, "%x", &first) == 1 && (first & 0xc0) == 0xc0) {
            return BLE_ADDR_RANDOM;
        }
    }
    return BLE_ADDR_PUBLIC;
}

// Diagnostic scan: logs every device seen so the real keyboard address/type
// can be confirmed on the serial monitor.
class ScanCallbacks : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice *ad) override
    {
        Serial.printf("[kbd-scan] %s type=%u rssi=%d name='%s'\n",
                      ad->getAddress().toString().c_str(),
                      (unsigned)ad->getAddress().getType(), ad->getRSSI(),
                      ad->haveName() ? ad->getName().c_str() : "");
    }
};
static ScanCallbacks g_scanCallbacks;

// Forwards a HID input report notification up to the module.
static void onHidNotify(NimBLERemoteCharacteristic *chr, uint8_t *data,
                        size_t len, bool)
{
    (void)chr;
    if (g_host != nullptr) {
        g_host->handleReport(data, len);
    }
}

// Reports a link drop back to the module and handles the pairing handshake.
class BleCallbacks : public NimBLEClientCallbacks {
public:
    void onConnect(NimBLEClient *client) override
    {
        Serial.printf("[kbd] onConnect %s\n",
                      client->getPeerAddress().toString().c_str());
        if (g_host != nullptr) {
            g_host->onConnected(client);
        }
    }

    void onConnectFail(NimBLEClient *, int reason) override
    {
        Serial.printf("[kbd] onConnectFail reason=%d\n", reason);
    }

    void onDisconnect(NimBLEClient *, int reason) override
    {
        Serial.printf("[kbd] onDisconnect reason=%d\n", reason);
        if (g_host != nullptr) {
            g_host->handleDisconnected();
        }
    }

    // The peer wants us (the host) to show a passkey and have it typed on the
    // keyboard. connectTo() pre-generates it, shows it on the status line and
    // installs it via setSecurityPasskey, so return that same value here.
    uint32_t onPassKeyDisplay(NimBLEConnInfo &) override
    {
        Serial.printf("[kbd] pairing: passkey display, returning %u\n",
                      (unsigned)NimBLEDevice::getSecurityPasskey());
        return NimBLEDevice::getSecurityPasskey();
    }

    // The peer asks us to enter a passkey it displays; we have no keyboard of
    // our own, so use the fixed pin.
    void onPassKeyEntry(NimBLEConnInfo &connInfo) override
    {
        Serial.printf("[kbd] pairing: passkey entry requested, injecting %u\n",
                      (unsigned)TERM_KBD_PIN);
        if (g_host != nullptr) {
            g_host->showPairPin(TERM_KBD_PIN);
        }
        NimBLEDevice::injectPassKey(connInfo, TERM_KBD_PIN);
    }

    // Numeric comparison: surface the number for the user to check and accept
    // it (this is the configured keyboard).
    void onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pass_key) override
    {
        Serial.printf("[kbd] pairing: confirm passkey %u\n", (unsigned)pass_key);
        if (g_host != nullptr) {
            g_host->showPairPin(pass_key);
        }
        NimBLEDevice::injectConfirmPasskey(connInfo, true);
    }

    void onAuthenticationComplete(NimBLEConnInfo &connInfo) override
    {
        if (!connInfo.isEncrypted()) {
            Serial.printf("[kbd] pairing FAILED (not encrypted)\n");
            return;
        }
        Serial.printf("[kbd] pairing OK (encrypted)\n");
    }
};
static BleCallbacks g_clientCallbacks;

void KbdHost::ensureRadio(void)
{
    if (init_) {
        return;
    }
    NimBLEDevice::init("ESP32-Terminal");
    NimBLEDevice::setPower(3);
    // Bonding with the first pair (Just Works; no MITM for keyboards without a
    // display). The old build shipped without security which is fine for
    // reports but most keyboards reject a link or refuse encrypted writes
    // until the host agrees to bond.
    NimBLEDevice::setSecurityAuth(true, false, true);
    init_ = true;
}

void KbdHost::radioInit(void)
{
    autoMode_ = TERM_KBD_AUTO;
    enabled_ = mac_[0] != '\0' || autoMode_;
    if (!enabled_) {
        return;
    }
    ensureRadio();
    Serial.printf("[kbd] radio up (early)\n");
}

void KbdHost::begin(const char *mac, TermKbdKeyCb keyCb, TermKbdStateCb stateCb,
                    TermKbdPairCb pairCb)
{
    keyCb_ = keyCb;
    stateCb_ = stateCb;
    pairCb_ = pairCb;
    if (mac != nullptr) {
        size_t n = strlen(mac);
        if (n >= sizeof(mac_)) {
            n = sizeof(mac_) - 1;
        }
        memcpy(mac_, mac, n);
        mac_[n] = '\0';
    } else {
        mac_[0] = '\0';
    }
    autoMode_ = TERM_KBD_AUTO;
    enabled_ = mac_[0] != '\0' || autoMode_;
    connected_ = false;
    wantConnect_ = enabled_;
    videoPaused_ = false;
    client_ = nullptr;
    lastTryMs_ = (uint32_t)-10000; // allow an immediate first connect attempt
    memset(lastKeys_, 0, sizeof(lastKeys_));
    g_host = this;
    queue_ = xQueueCreate(16, sizeof(KbdEvent));
    if (!enabled_) {
        return;
    }
    ensureRadio();
    // NOTE(debug): security disabled to isolate whether the pairing handshake
    // blocks the connect -> GATT discovery flow against the Cardputer, which
    // does not configure security at all.
    Serial.printf("[kbd] host up, target %s (pin %u, auto=%d)\n", mac_,
                  (unsigned)TERM_KBD_PIN, autoMode_ ? 1 : 0);
}

void KbdHost::scanOnce(unsigned seconds)
{
    ensureRadio();
    Serial.printf("[kbd] scan %us: put the keyboard in pairing mode now\n",
                  seconds);
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_scanCallbacks);
    scan->setActiveScan(true);
    scan->setInterval(80);
    scan->setWindow(80);
    scan->clearResults();
    scan->start(seconds, false);
    NimBLEScanResults results = scan->getResults();
    Serial.printf("[kbd] scan done: %u device(s)\n", (unsigned)results.getCount());
}

void KbdHost::poll(void)
{
    drainQueue();
    if (!enabled_ || connected_) {
        return;
    }

    uint32_t now = millis();
    uint32_t retryMs = autoMode_ ? 2000 : 10000;
    if ((now - lastTryMs_) < retryMs) {
        return;
    }

    // Auto mode: the keyboard rotates its address, so rediscover it on every
    // attempt and connect to the live address instead of a fixed MAC.
    if (autoMode_) {
        lastTryMs_ = now;
        NimBLEAddress found;
        uint8_t type = BLE_ADDR_PUBLIC;
        // Some keyboards only advertise in short bursts while pairing (a
        // single adv every few seconds), so a long window beats several short
        // ones. Keep scanning until seen, then connect immediately.
        if (!discoverTarget(20, &found, &type)) {
            Serial.printf("[kbd] auto: no keyboard advertised, retry\n");
            return;
        }
        Serial.printf("[kbd] auto: found, connecting %s type=%u\n",
                      found.toString().c_str(), (unsigned)type);
        connectToAddress(found);
        return;
    }

    // Issue a fresh connect attempt only when there is no live link: either no
    // client was ever created, or the existing one is disconnected (the
    // reconnect after a drop). A CONNECTING client must never be re-entered,
    // but with the synchronous connect() below the attempt blocks until the
    // state settles (CONNECTED or failed), so poll() never observes CONNECTING.
    NimBLEClient *active = NimBLEDevice::getClientByPeerAddress(
        NimBLEAddress{std::string(mac_), kbdAddrType(mac_)});
    bool idle = (active == nullptr) || !active->isConnected();
    if (wantConnect_ && idle) {
        lastTryMs_ = now;
        Serial.printf("[kbd] attempting connect\n");
        connectTo();
    }
}

void KbdHost::drainQueue(void)
{
    if (queue_ == nullptr) {
        return;
    }
    QueueHandle_t q = (QueueHandle_t)queue_;
    KbdEvent ev;
    while (xQueueReceive(q, &ev, 0) == pdTRUE) {
        if (ev.type == 0 && keyCb_ != nullptr) {
            keyCb_(ev.keycode, ev.mods);
        } else if (ev.type == 1) {
            // Link up/down reported from the NimBLE task; the subscribe itself
            // happens inline in connectTo(), so here we only reflect state.
            setState(ev.connected);
        } else if (ev.type == 2 && pairCb_ != nullptr) {
            pairCb_(ev.pin);
        }
    }
}

void KbdHost::showPairPin(uint32_t pin)
{
    if (queue_ == nullptr) {
        return;
    }
    QueueHandle_t q = (QueueHandle_t)queue_;
    KbdEvent ev = {2, 0, 0, false, pin};
    xQueueSend(q, &ev, 0);
}

void KbdHost::handleDisconnected(void)
{
    if (queue_ == nullptr) {
        return;
    }
    QueueHandle_t q = (QueueHandle_t)queue_;
    KbdEvent ev = {1, 0, 0, false};
    xQueueSend(q, &ev, 0);
}

void KbdHost::setState(bool connected)
{
    bool prev = connected_;
    connected_ = connected;
    wantConnect_ = enabled_ && !connected;
    if (!connected) {
        // Reconnect promptly after a drop instead of waiting out the retry
        // interval (the video is only paused briefly during the attempt).
        lastTryMs_ = 0;
    }
    if (connected != prev && stateCb_ != nullptr) {
        stateCb_(connected);
    }
}

void KbdHost::handleReport(const uint8_t *data, size_t len)
{
    if (len < 2) {
        return;
    }
    if (data[1] != 0) {
        Serial.printf("[kbd] report key=%02x mods=%02x\n",
                      (unsigned)data[1], (unsigned)data[0]);
    }
    if (len < 2 || queue_ == nullptr) {
        return;
    }
    QueueHandle_t q = (QueueHandle_t)queue_;
    const uint8_t mods = data[0];
    // Fire once per key that is newly present (was not held in the last
    // report), so auto-repeat and modifier noise don't produce repeats.
    for (size_t i = 2; i < len && i < 8; i++) {
        const uint8_t k = data[i];
        if (k < TERM_KBD_HID_KEY_MIN) {
            continue;
        }
        bool wasDown = false;
        for (int p = 0; p < 6; p++) {
            if (lastKeys_[p] == k) {
                wasDown = true;
                break;
            }
        }
        if (!wasDown) {
            KbdEvent ev = {0, k, mods, false};
            xQueueSend(q, &ev, 0);
        }
    }
    // Snapshot the current keys for the next edge detection.
    int w = 0;
    for (size_t i = 2; i < len && i < 8; i++) {
        const uint8_t k = data[i];
        if (k >= TERM_KBD_HID_KEY_MIN) {
            lastKeys_[w++] = k;
        }
    }
    for (; w < 6; w++) {
        lastKeys_[w] = 0;
    }
}

// Auto-discovery target picker: prefers an advertised device whose name looks
// like a keyboard; falls back to the strongest named device when no keyboard
// name is advertised (single-peripheral setups). Must outlive any single scan:
// NimBLE keeps the pointer installed until the next setScanCallbacks / clears
// the scan-response timer, so a stack instance would dangle and crash.
class TargetCallbacks : public NimBLEScanCallbacks {
public:
    NimBLEAddress kbdAddr;
    bool haveKbd = false;
    int kbdRssi = -1000;
    NimBLEAddress anyAddr;
    bool haveAny = false;
    int anyRssi = -1000;

    void reset(void)
    {
        haveKbd = false;
        haveAny = false;
        kbdRssi = -1000;
        anyRssi = -1000;
    }

    void onResult(const NimBLEAdvertisedDevice *ad) override
    {
        const char *name = ad->haveName() ? ad->getName().c_str() : nullptr;
        const char *addrStr = ad->getAddress().toString().c_str();
        const int rssi = ad->getRSSI();
        Serial.printf("[kbd-auto] %s type=%u rssi=%d name='%s'\n", addrStr,
                      (unsigned)ad->getAddress().getType(), rssi,
                      name != nullptr ? name : "");
        bool kbdName = false;
        if (name != nullptr && name[0] != '\0') {
            const char *n = name;
            kbdName = strstr(n, "keyboard") != nullptr ||
                      strstr(n, "Keyboard") != nullptr ||
                      strstr(n, "KEYBOARD") != nullptr ||
                      strstr(n, "teclad") != nullptr ||
                      strstr(n, "Keyb") != nullptr;
        }
        bool kbdPrefix = TERM_KBD_PREFIX[0] != '\0' &&
                         strncmp(addrStr, TERM_KBD_PREFIX,
                                 strlen(TERM_KBD_PREFIX)) == 0;
        if (kbdName || kbdPrefix) {
            if (rssi > kbdRssi) {
                kbdAddr = ad->getAddress();
                kbdRssi = rssi;
                haveKbd = true;
            }
        } else if (name != nullptr && name[0] != '\0' && rssi > anyRssi) {
            anyAddr = ad->getAddress();
            anyRssi = rssi;
            haveAny = true;
        }
    }

    bool pick(NimBLEAddress *out, uint8_t *type) const
    {
        if (haveKbd) {
            *out = kbdAddr;
            *type = kbdAddr.getType();
            return true;
        }
        if (haveAny) {
            *out = anyAddr;
            *type = anyAddr.getType();
            return true;
        }
        return false;
    }
};
static TargetCallbacks g_targetCallbacks;

bool KbdHost::discoverTarget(unsigned seconds, NimBLEAddress *out,
                             uint8_t *addrType)
{
    g_targetCallbacks.reset();
    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_targetCallbacks);
    scan->setActiveScan(true);
    scan->setInterval(80);
    scan->setWindow(80);
    scan->clearResults();
    scan->start(seconds, false);
    // Scan events are delivered by the NimBLE host task, which may still be
    // reporting the last advertiser seen after start() returns. Give the task a
    // moment to drain before picking, or a burst advertiser is always missed.
    delay(250);
    return g_targetCallbacks.pick(out, addrType);
}

void KbdHost::connectTo(void)
{
    if (mac_[0] == '\0') {
        return;
    }
    NimBLEAddress addr{std::string(mac_), kbdAddrType(mac_)};
    connectToAddress(addr);
}

void KbdHost::connectToAddress(NimBLEAddress &addr)
{
    // Auto mode rotates the address every few seconds, so we cannot key clients
    // by peer address: creating a new NimBLEClient per address exhausts the
    // (small) client pool and every later attempt ends in "createClient failed".
    // Instead reuse ONE persistent client, pointing `connect(addr)` at the live
    // address on every attempt. On failure we delete it so the next try starts
    // clean (a half-open client refuses to connect again with BLE_HS_EALREADY).
    NimBLEClient *client = client_;
    // Auto mode rotates the address, and an old client holds attributes of a
    // stale link that can't be reattached to a new peer. connectToAddress is
    // only reached from poll() when connected_==false, so a leftover client is
    // always a finished link -- drop it and recreate fresh each attempt. (With
    // the synchronous connect() below we never observe a mid-CONNECTING client
    // here, so there is no in-flight link to preserve.)
    if (client != nullptr) {
        recycleClient();
        client = nullptr;
    }
    bool fresh = true;
    client = NimBLEDevice::createClient();
    if (client == nullptr) {
        Serial.printf("[kbd] createClient failed\n");
        return;
    }
    client_ = client;
    client->setClientCallbacks(&g_clientCallbacks, /*deleteCallbacks=*/false);
    Serial.printf("[kbd] connectTo %s (type=%u fresh=%d)\n",
                  addr.toString().c_str(), (unsigned)addr.getType(), fresh ? 1 : 0);
    // Short connect timeout: while the video is paused the CRT shows a frozen
    // frame, so a failed connect must not hold it for the 30s library default.
    // Auto mode extends it: a burst-advertising keyboard may be mid-gap when
    // the connect lands and answers on its next adv.
    client->setConnectTimeout(autoMode_ ? 10000 : 3000);
    // Pause the NTSC video DMA so the BLE controller is not starved while the
    // link is established and the HID service is discovered/subscribed.
    if (!videoPaused_) {
        video_pause();
        videoPaused_ = true;
    }
    // Synchronous connect: blocks on the main-loop task until the link is
    // established (m_connStatus = CONNECTED) with a clean client state. The
    // async path left the client in a half-CONNECTING state that raced the
    // service discovery and caused ATT errors / crashes. poll() gates retries
    // on active==nullptr so it never re-enters while this is in flight.
    bool ok = client->connect(addr, /*deleteAttributes=*/fresh,
                              /*asyncConnect=*/false, /*exchangeMTU=*/false);
    if (!ok) {
        Serial.printf("[kbd] connect() failed rc=%d\n", client->getLastError());
        if (videoPaused_) {
            video_resume();
            videoPaused_ = false;
        }
        recycleClient();
        return;
    }
    Serial.printf("[kbd] connected sync (state=%d)\n", client->isConnected());
    // Split the GATT work in two so the fragility doesn't sit behind a paused
    // DMA: here we only discover services + pair (secureConnection) + subscribe
    // while the video is still paused -- all of it reliable for this keyboard
    // peer, which answers GATT only over a bonded link.
    if (discoverServices(client)) {
        connected_ = true;
        Serial.printf("[kbd] connected + subscribed\n");
        if (videoPaused_) {
            video_resume();
            videoPaused_ = false;
        }
        if (stateCb_ != nullptr) {
            stateCb_(true);
        }
        return;
    }
    if (videoPaused_) {
        video_resume();
        videoPaused_ = false;
    }
    Serial.printf("[kbd] service discovery/pairing failed (no drop)\n");
}

void KbdHost::recycleClient(void)
{
    // Drop a stale/half-open client so auto mode can connect again. Deleting a
    // still-connected client schedules the delete for when it disconnects;
    // disconnected ones are freed immediately, freeing the pool slot.
    if (client_ != nullptr) {
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
    }
}

void KbdHost::onConnected(NimBLEClient *client)
{
    // The NimBLE host runs on its own task; doing a synchronous GATT service
    // discovery here would block that same task waiting on its own GATT
    // callbacks (deadlock). Instead we only record that the link is up; poll()
    // on the main-loop task performs the discovery/subscribe, where the GATT
    // callbacks can still be serviced.
    Serial.printf("[kbd] onConnected (recorded)\n");
    if (queue_ == nullptr) {
        return;
    }
    KbdEvent ev = {1, 0, 0, true};
    xQueueSend((QueueHandle_t)queue_, &ev, 0);
}

bool KbdHost::discoverServices(NimBLEClient *client)
{
    if (client == nullptr) {
        return false;
    }
    if (!client->isConnected()) {
        return false;
    }
    // Discover the full attribute table (services) once. Using a single
    // synchronous discovery is required: issuing several overlapping service
    // discoveries from the main loop races the shared GATT callback in NimBLE
    // 2.5.0 and corrupts the client (crash / zero services).
    const auto &svcs = client->getServices(true);
    for (const auto *raw : svcs) {
        Serial.printf("[kbd]   svc %s\n", raw->getUUID().toString().c_str());
    }
    if (!client->isConnected() || client->getService(kHidService) == nullptr) {
        return false;
    }
    // Do NOT force a secureConnection/pairing here: this keyboard's firmware
    // drops the link (reason 531) right after the encrypt handshake completes,
    // before the report characteristic can be discovered. HID input reports
    // are delivered over the plain (unencrypted) link once the CCCD is written,
    // so skip pairing altogether and subscribe directly. SecureConnection is
    // only needed if a peer rejects the subscribe on an unencrypted link.
    return subscribeHid(client);
}

bool KbdHost::subscribeHid(NimBLEClient *client)
{
    if (client == nullptr || !client->isConnected()) {
        Serial.printf("[kbd] not connected for subscribe\n");
        return false;
    }
    bool subscribed = false;
    NimBLERemoteService *svc = client->getService(kHidService);
    if (svc != nullptr) {
        // Pick the characteristic that actually carries notifications instead of
        // guessing Boot(0x2a22) first. A HOGP keyboard exposes either the Boot
        // Input Report (0x2a22, the Cardputer) or one or more Report (0x2a4d)
        // characteristics (the budget keyboard). The budget peer exposes SEVERAL
        // 0x2a4d reports (input, output, feature...), and getCharacteristic(0x2a4d)
        // returns only the LAST one discovered (the CB overwrites m_pBuf each
        // match), which is the feature report with no NOTIFY -- so the old
        // per-UUID lookup silently failed and no input report was ever subscribed.
        // Iterate every characteristic and pick the first with the NOTIFY property
        // that is a read/notify input report; skip write-only/indicate reports.
        NimBLERemoteCharacteristic *chr = nullptr;
        for (const auto *c : svc->getCharacteristics(true)) {
            const NimBLEUUID u = c->getUUID();
            if (u != NimBLEUUID(kBootInputChr) && u != NimBLEUUID(kReportChr)) {
                continue;
            }
            if (c->canNotify() && c->canRead() && !c->canWrite()) {
                chr = const_cast<NimBLERemoteCharacteristic *>(c);
                break;
            }
        }
        // Fall back to any notify report if no read-only input report was found.
        if (chr == nullptr) {
            for (const auto *c : svc->getCharacteristics(true)) {
                if (c->canNotify()) {
                    chr = const_cast<NimBLERemoteCharacteristic *>(c);
                    break;
                }
            }
        }
        if (chr == nullptr) {
            Serial.printf("[kbd] HID svc has no notify input report chr\n");
            return false;
        }
        // NimBLE's setNotify() silently returns true when the CCCD descriptor is
        // missing (it only sets the callback), so the enable never reaches the
        // peer and the keyboard sends nothing -- yet subscribe() "succeeds".
        // Force the descriptors (incl. the CCCD) to be present before trusting
        // the subscribe. Some budget HID peers accept the CCCD write but do not
        // reflect it on a later read, so trust the write itself rather than a
        // read-back (which is a false negative on those peers).
        const uint16_t kCccd = 0x2902;
        bool haveCccd = false;
        if (client->isConnected()) {
            for (NimBLERemoteDescriptor *d : chr->getDescriptors(true)) {
                if (d->getUUID() == NimBLEUUID(kCccd)) {
                    haveCccd = true;
                    break;
                }
            }
        }
        Serial.printf("[kbd] HID chr u=%s notify=%d cccd=%d\n",
                      chr->getUUID().toString().c_str(), chr->canNotify() ? 1 : 0,
                      haveCccd ? 1 : 0);
        if (haveCccd && client->isConnected()) {
            // Write without a response first (the CCCD write that most peers
            // accept), falling back to a write with response.
            subscribed = chr->subscribe(true, onHidNotify, /*response=*/false);
            if (!subscribed && client->isConnected()) {
                subscribed = chr->subscribe(true, onHidNotify, /*response=*/true);
            }
        }
        Serial.printf("[kbd] HID subscribe done=%d (had cccd=%d)\n",
                      subscribed ? 1 : 0, haveCccd ? 1 : 0);
    } else {
        Serial.printf("[kbd] HID service 0x%04x NOT found\n", kHidService);
    }
    return subscribed;
}
