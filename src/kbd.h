#ifndef TERM_KBD_H
#define TERM_KBD_H

#include <stddef.h>
#include <stdint.h>

class NimBLEClient;

// Set when a USB HID report encodes a key in its normal state (not a
// modifier/capslock toggle or an unmapped usage).
#define TERM_KBD_HID_KEY_MIN 0x04

// Callback fired once per newly-pressed (edge) key with the USB HID keycode
// and the report's modifier bits (see keymap.h Modifier).
typedef void (*TermKbdKeyCb)(uint8_t keycode, uint8_t mods);

// Callback fired when the keyboard connects or drops.
typedef void (*TermKbdStateCb)(bool connected);

// Callback fired during pairing so the host can show the passkey the user
// must type on the keyboard. Pin 0 means "just confirm" (numeric compare).
typedef void (*TermKbdPairCb)(uint32_t pin);

// BLE HID host (HOGP) that connects to one fixed keyboard by address,
// subscribes to its HID input report and feeds key presses to TermKbdKeyCb.
// Hardware-bound: not unit-tested on the host; the pure KeyMap seam upstream
// is where the scancode -> byte mapping is verified.
//
// The NimBLE host runs on its own task, so the HID/state notifications only
// enqueue events; poll() drains them on the caller's task so the callbacks
// always run on the main loop thread.
struct KbdEvent;
class KbdHost {
public:
    void begin(const char *mac, TermKbdKeyCb keyCb, TermKbdStateCb stateCb,
               TermKbdPairCb pairCb);
    void poll(void); // drive (re)connection + manual report reads from the loop
    void radioInit(void); // init the BLE radio early, before the SSH stack

    // Discovery: scan for `seconds` and log every device seen (address, type,
    // rssi, name) so the real keyboard MAC can be put into TERM_KBD_MAC.
    // Blocks the caller for `seconds`; typically called once from setup().
    void scanOnce(unsigned seconds);

    bool connected(void) const { return connected_; }
    bool enabled(void) const { return enabled_; }

    // Feed a raw HID report into the module (used by the characteristic
    // notification callback). Detects key press/release edges. May run on
    // the NimBLE task; only enqueues events.
    void handleReport(const uint8_t *data, size_t len);

    // Called by the NimBLE client callback when the link drops. May run on
    // the NimBLE task; only enqueues an event.
    void handleDisconnected(void);

    // Called by the NimBLE client callback once the link is up. May run on
    // the NimBLE task; performs the HID service discovery + subscribe and
    // marks the connection established.
    void onConnected(NimBLEClient *client);

    // Called by the NimBLE pairing callbacks to surface a passkey the user
    // must confirm/type. May run on the NimBLE task; only enqueues an event.
    void showPairPin(uint32_t pin);

private:
    void connectTo(void);
    void connectToAddress(class NimBLEAddress &addr);
    bool discoverTarget(unsigned seconds, class NimBLEAddress *out,
                        uint8_t *addrType);
    bool discoverServices(class NimBLEClient *client);
    bool subscribeHid(NimBLEClient *client);
    void recycleClient(void);
    void drainQueue(void);
    void setState(bool connected);
    void ensureRadio(void);

    TermKbdKeyCb keyCb_;
    TermKbdStateCb stateCb_;
    TermKbdPairCb pairCb_;
    char mac_[18];
    void *queue_; // QueueHandle_t of KbdEvent
    bool enabled_;
    bool connected_;
    bool wantConnect_;
    bool videoPaused_;
    bool init_;
    bool autoMode_;
    uint32_t lastTryMs_;
    uint8_t lastKeys_[6];
    class NimBLEClient *client_; // single reusable client in auto mode
};

#endif
