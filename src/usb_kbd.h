#ifndef TERM_USB_KBD_H
#define TERM_USB_KBD_H

#include <stddef.h>
#include <stdint.h>

// Key callback fired per newly-pressed key with the USB HID keycode and the
// report's modifier bits (see keymap.h Modifier).
typedef void (*TermKbdKeyCb)(uint8_t keycode, uint8_t mods);

// Status callback fired when a keyboard connects or drops.
typedef void (*TermKbdStateCb)(bool connected);

// USB Host HID boot-keyboard reader built on the ESP-IDF USB Host Library
// (usb_host.h). Uses the ESP32-S3 native USB-OTG port as a host: claims the
// first HID boot-keyboard interface it finds, sets the boot protocol and
// queues an interrupt IN transfer for the 8-byte report. Hardware-bound, not
// unit-tested on the host; the pure KeyMap seam upstream is where the keycode
// -> byte mapping is verified.
class UsbHost {
public:
    void begin(TermKbdKeyCb keyCb, TermKbdStateCb stateCb);
    void poll(void); // drive the host/client event loops from the main loop

    bool connected(void) const { return connected_; }

    // Called by the static transfer/client callbacks (run on the caller's
    // thread via usb_host_client_handle_events()).
    void openDevice(uint8_t devAddr);
    void handleReport(void *transfer); // usb_transfer_t *

private:
    void setState(bool connected);

    TermKbdKeyCb keyCb_;
    TermKbdStateCb stateCb_;
    void *client_;     // usb_host_client_handle_t
    void *devHandle_;  // usb_device_handle_t
    uint8_t intfNum_;
    uint8_t epInAddr_;
    void *transfer_;   // usb_transfer_t *
    bool connected_;
    uint8_t lastKeys_[6];
    bool claimed_;
};

#endif
