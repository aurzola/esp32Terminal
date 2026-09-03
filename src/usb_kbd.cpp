#include "usb_kbd.h"

#include <stdarg.h>
#include <string.h>

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "usb/usb_types_ch9.h"
#include "usb/usb_types_stack.h"

// HID boot-keyboard report: modifiers byte, reserved byte, then 6 keycodes.
static const uint8_t kReportLen = 8;
static const uint8_t kHidKeyMin = 0x04;

// Published by the sketch so the USB host diagnostics can be drawn on the CRT
// status line (no serial monitor needed on the second port).
extern "C" void termUsbDebug(const char *text);

static UsbHost *g_host = nullptr;

// Logs to the serial monitor and mirrors the text to the CRT status line.
static void usbLog(const char *fmt, ...)
{
    char buf[48];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("[usb-kbd] %s\n", buf);
    termUsbDebug(buf);
}

// Transfer completion callback: runs from usb_host_client_handle_events().
// Parse the report, forward key edges, and re-queue the next report.
static void reportCb(usb_transfer_t *transfer)
{
    if (g_host != nullptr) {
        g_host->handleReport(transfer);
    }
}

// Client event callback: runs from usb_host_client_handle_events().
static void clientEventCb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    if (g_host != nullptr && msg != nullptr &&
        msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        usbLog("NEW_DEV addr=%d", msg->new_dev.address);
        g_host->openDevice(msg->new_dev.address);
    } else if (msg != nullptr &&
               msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        usbLog("DEV_GONE");
    }
}

void UsbHost::begin(TermKbdKeyCb keyCb, TermKbdStateCb stateCb)
{
    keyCb_ = keyCb;
    stateCb_ = stateCb;
    client_ = nullptr;
    devHandle_ = nullptr;
    transfer_ = nullptr;
    intfNum_ = 0;
    epInAddr_ = 0;
    connected_ = false;
    claimed_ = false;
    memset(lastKeys_, 0, sizeof(lastKeys_));
    g_host = this;

    const usb_host_config_t hostCfg = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = nullptr,
        .fifo_settings_custom = {0, 0, 0},
        .peripheral_map = 0,
    };
    if (usb_host_install(&hostCfg) != ESP_OK) {
        usbLog("usb_host_install failed");
        return;
    }
    const usb_host_client_config_t clientCfg = {
        .is_synchronous = false,
        .max_num_event_msg = 3,
        .async = {
            .client_event_callback = clientEventCb,
            .callback_arg = nullptr,
        },
    };
    if (usb_host_client_register(&clientCfg,
                                 (usb_host_client_handle_t *)&client_) != ESP_OK) {
        usbLog("client register failed");
        return;
    }
    usbLog("host up");
}

void UsbHost::poll(void)
{
    uint32_t libEvents = 0;
    usb_host_lib_handle_events(0, &libEvents);
    if (client_ != nullptr) {
        usb_host_client_handle_events((usb_host_client_handle_t)client_, 0);
    }
    static uint32_t lastLog = 0;
    uint32_t now = millis();
    if (now - lastLog >= 5000) {
        lastLog = now;
        usb_host_lib_info_t info = {};
        if (usb_host_lib_info(&info) == ESP_OK) {
            usbLog("devices=%d clients=%d connected=%d",
                   info.num_devices, info.num_clients, connected_ ? 1 : 0);
        }
    }
}

void UsbHost::setState(bool connected)
{
    bool prev = connected_;
    connected_ = connected;
    if (connected != prev && stateCb_ != nullptr) {
        stateCb_(connected);
    }
}

void UsbHost::handleReport(void *v)
{
    usb_transfer_t *t = (usb_transfer_t *)v;
    if (t->status == USB_TRANSFER_STATUS_COMPLETED &&
        t->actual_num_bytes >= 2) {
        const uint8_t mods = t->data_buffer[0];
        for (int i = 2; i < t->actual_num_bytes && i < 8; i++) {
            const uint8_t k = t->data_buffer[i];
            if (k < kHidKeyMin) {
                continue;
            }
            bool wasDown = false;
            for (int p = 0; p < 6; p++) {
                if (lastKeys_[p] == k) {
                    wasDown = true;
                    break;
                }
            }
            if (!wasDown && keyCb_ != nullptr) {
                keyCb_(k, mods);
            }
        }
        int w = 0;
        for (int i = 2; i < t->actual_num_bytes && i < 8; i++) {
            const uint8_t k = t->data_buffer[i];
            if (k >= kHidKeyMin) {
                lastKeys_[w++] = k;
            }
        }
        for (; w < 6; w++) {
            lastKeys_[w] = 0;
        }
    }
    // Re-queue for the next report.
    if (t->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        setState(false);
        return;
    }
    t->num_bytes = kReportLen;
    if (usb_host_transfer_submit(t) != ESP_OK) {
        usbLog("re-submit failed");
    }
}

void UsbHost::openDevice(uint8_t devAddr)
{
    if (devHandle_ != nullptr) {
        return; // already have a keyboard
    }
    usb_device_handle_t dev;
    if (usb_host_device_open((usb_host_client_handle_t)client_, devAddr,
                             &dev) != ESP_OK) {
        usbLog("device_open failed addr=%d", devAddr);
        return;
    }
    const usb_config_desc_t *cfg = nullptr;
    if (usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK) {
        usbLog("get_config_desc failed");
        usb_host_device_close((usb_host_client_handle_t)client_, dev);
        return;
    }
    usbLog("opened addr=%d cfgLen=%u", devAddr,
                  cfg->wTotalLength);

    // Walk the config descriptor for a HID boot-keyboard interface: class 3,
    // subclass 1 (boot), protocol 1 (keyboard).
    int off = 0;
    const usb_standard_desc_t *d = (const usb_standard_desc_t *)cfg;
    while ((d = usb_parse_next_descriptor(d, cfg->wTotalLength, &off)) !=
           nullptr) {
        if (d->bDescriptorType != USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            continue;
        }
        const usb_intf_desc_t *itf = (const usb_intf_desc_t *)d;
        if (itf->bInterfaceClass != 0x03 || itf->bInterfaceSubClass != 0x01 ||
            itf->bInterfaceProtocol != 0x01) {
            continue;
        }
        int eoff = 0;
        const usb_ep_desc_t *ep =
            (const usb_ep_desc_t *)usb_parse_endpoint_descriptor_by_index(
                itf, 0, cfg->wTotalLength, &eoff);
        if (ep == nullptr || (ep->bmAttributes & 0x03) != 0x03 ||
            (ep->bEndpointAddress & 0x80) == 0) {
            continue;
        }
        if (usb_host_interface_claim((usb_host_client_handle_t)client_, dev,
                                     itf->bInterfaceNumber, 0) != ESP_OK) {
            continue;
        }
        devHandle_ = dev;
        intfNum_ = itf->bInterfaceNumber;
        epInAddr_ = ep->bEndpointAddress;
        claimed_ = true;
        break;
    }

    if (!claimed_) {
        usbLog("no boot-keyboard interface found");
        usb_host_device_close((usb_host_client_handle_t)client_, dev);
        return;
    }
    if (usb_host_transfer_alloc(kReportLen, 0,
                                (usb_transfer_t **)&transfer_) != ESP_OK) {
        usbLog("transfer alloc failed");
        return;
    }
    usb_transfer_t *t = (usb_transfer_t *)transfer_;
    t->device_handle = dev;
    t->bEndpointAddress = epInAddr_;
    t->num_bytes = kReportLen;
    t->callback = reportCb;
    if (usb_host_transfer_submit(t) != ESP_OK) {
        usbLog("transfer submit failed");
        return;
    }
    setState(true);
}
