/*
 * M5Cardputer as a BLE HID keyboard for the ESP32-Terminal CRT.
 *
 * Reads the Cardputer's physical keyboard and advertises it as a BLE HID
 * keyboard (HOGP). The terminal (esp32Terminal.ino) acts as the BLE central,
 * connects to this device's MAC (set in the terminal's TERM_KBD_MAC) and
 * receives the keystrokes as boot-keyboard reports.
 *
 * The HID service is built manually (0x1812 + boot keyboard input report
 * 0x2a22 + report map 0x2a4b) so it exactly matches the layout the terminal's
 * KbdHost discovers; NimBLEHIDDevice did not register the GATT database
 * correctly on this board's SDK.
 */

#include <Arduino.h>
#include <M5Cardputer.h>
#include <NimBLEDevice.h>
#include <NimBLEUtils.h>
#include <host/ble_hs.h>
#include <host/ble_gatt.h>

static NimBLECharacteristic *g_bootInput = nullptr;

static void showLink(const char *text, uint16_t color)
{
    M5Cardputer.Display.setTextColor(color);
    M5Cardputer.Display.fillRect(0, M5Cardputer.Display.height() - 20,
                                 M5Cardputer.Display.width(), 20, BLACK);
    M5Cardputer.Display.drawString(text, M5Cardputer.Display.width() / 2,
                                   M5Cardputer.Display.height() - 10);
}

class ServerCallbacks : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override
    {
        Serial.printf("SVC: connect from %s\n",
                      connInfo.getAddress().toString().c_str());
        showLink("CONNECTED", GREEN);
    }
    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo,
                      int reason) override
    {
        Serial.printf("SVC: disconnect reason=%d\n", reason);
        // Restart advertising so the terminal can reconnect after a drop
        // (NimBLE does not auto-restart the advertiser on disconnect).
        NimBLEDevice::startAdvertising();
        showLink("ADVERTISING", ORANGE);
    }
};

class ChrCallbacks : public NimBLECharacteristicCallbacks {
public:
    void onRead(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo) override
    {
        Serial.printf("CHR: read %s\n", chr->getUUID().toString().c_str());
    }
    void onSubscribe(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo,
                     uint16_t subValue) override
    {
        Serial.printf("CHR: subscribe %s val=%u\n",
                      chr->getUUID().toString().c_str(), subValue);
    }
};

// Boot keyboard report descriptor (8-byte report: 2 modifier/reserved +
// 6 keycodes).
static const uint8_t kReportMap[] = {
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xA1, 0x01, // Collection (Application)
    0x05, 0x07, // Usage Page (Key Codes)
    0x19, 0xE0, // Usage Minimum (224)
    0x29, 0xE7, // Usage Maximum (231)
    0x15, 0x00, // Logical Minimum (0)
    0x25, 0x01, // Logical Maximum (1)
    0x75, 0x01, // Report Size (1)
    0x95, 0x08, // Report Count (8)
    0x81, 0x02, // Input (Data, Variable, Absolute)
    0x95, 0x01, // Report Count (1)
    0x75, 0x08, // Report Size (8)
    0x81, 0x01, // Input (Constant)
    0x95, 0x05, // Report Count (5)
    0x75, 0x01, // Report Size (1)
    0x05, 0x08, // Usage Page (LEDs)
    0x19, 0x01, // Usage Minimum (1)
    0x29, 0x05, // Usage Maximum (5)
    0x91, 0x02, // Output (Data, Variable, Absolute)
    0x95, 0x01, // Report Count (1)
    0x75, 0x03, // Report Size (3)
    0x91, 0x01, // Output (Constant)
    0x95, 0x06, // Report Count (6)
    0x75, 0x08, // Report Size (8)
    0x15, 0x00, // Logical Minimum (0)
    0x25, 0x65, // Logical Maximum (101)
    0x05, 0x07, // Usage Page (Key Codes)
    0x19, 0x00, // Usage Minimum (0)
    0x29, 0x65, // Usage Maximum (101)
    0x81, 0x00, // Input (Data, Array)
    0xC0        // End Collection
};

void setup(void)
{
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.setTextDatum(middle_center);
    M5Cardputer.Display.setTextFont(&fonts::Orbitron_Light_24);
    M5Cardputer.Display.drawString("BLE KB",
                                   M5Cardputer.Display.width() / 2,
                                   M5Cardputer.Display.height() / 2);

    // Initialize BLE after the Cardputer so its startup (USB/keyboard init)
    // does not fight NimBLE for the interrupt / IPC resources. The larger
    // task stacks are set via build flags to avoid the IPC-task overflow.
    NimBLEDevice::init("Cardputer-KB");
    NimBLEDevice::setPower(3);
    NimBLEAddress localAddr = NimBLEDevice::getAddress();
    Serial.printf("local addr: %s type=%d\n", localAddr.toString().c_str(),
                  static_cast<int>(localAddr.getType()));

    // HID service (0x1812) with the boot keyboard input report (0x2a22),
    // report map (0x2a4b) and HID information (0x2a4a).
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    NimBLEService *hid = server->createService(NimBLEUUID((uint16_t)0x1812));

    NimBLECharacteristic *hidInfo =
        hid->createCharacteristic(NimBLEUUID((uint16_t)0x2a4a),
                                  NIMBLE_PROPERTY::READ);
    const uint8_t hidInfoVal[4] = {0x00, 0x01, 0x00, 0x00}; // bcdHID 1.0
    hidInfo->setValue(hidInfoVal, sizeof(hidInfoVal));

    NimBLECharacteristic *reportMap =
        hid->createCharacteristic(NimBLEUUID((uint16_t)0x2a4b),
                                  NIMBLE_PROPERTY::READ);
    reportMap->setValue(kReportMap, sizeof(kReportMap));
    reportMap->setCallbacks(new ChrCallbacks());

    g_bootInput = hid->createCharacteristic(
        NimBLEUUID((uint16_t)0x2a22), NIMBLE_PROPERTY::NOTIFY);
    g_bootInput->setCallbacks(new ChrCallbacks());

    bool srvStart = server->start();
    Serial.printf("server start: %s\n", srvStart ? "ok" : "FAIL");

    // Confirm the service is actually registered in the local GATT database
    // (ble_gatts) so a peer running service discovery can see it.
    delay(50);
    ble_uuid16_t hidUuid = BLE_UUID16_INIT(0x1812);
    uint16_t hidHandle = 0;
    int rc = ble_gatts_find_svc(&hidUuid.u, &hidHandle);
    Serial.printf("local HID svc handle: rc=%d handle=%u (0x%04x)\n", rc,
                  hidHandle, hidHandle);

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->enableScanResponse(true);
    adv->setName("Cardputer-KB");
    adv->addServiceUUID(NimBLEUUID((uint16_t)0x1812));
    bool advStart = NimBLEDevice::startAdvertising();
    Serial.printf("advertising start: %s\n", advStart ? "ok" : "FAIL");
    showLink("ADVERTISING", ORANGE);

    // Re-check the service is still registered after advertising (a GATT reset
    // triggered by startAdvertising would empty the table).
    delay(50);
    uint16_t hidHandle2 = 0;
    int rc2 = ble_gatts_find_svc(&hidUuid.u, &hidHandle2);
    Serial.printf("post-adv HID handle: rc=%d handle=%u\n", rc2, hidHandle2);

    Serial.printf("Cardputer BLE addr: %s\n",
                  NimBLEDevice::getAddress().toString().c_str());
    Serial.printf("Cardputer BLE HID keyboard up\n");
}

void loop(void)
{
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange()) {
        return;
    }

    // Send the full state on EVERY change, press AND release. The terminal's
    // KbdHost detects key edges against its own snapshot (lastKeys_), so it
    // needs the key-up (empty) report to clear it — otherwise re-pressing the
    // same key (e.g. backspace twice in a row) never fires again.
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
    uint8_t report[8] = {0};
    report[0] = status.modifiers;
    uint8_t index = 0;
    for (auto k : status.hid_keys) {
        if (index >= 6) {
            break;
        }
        report[2 + index] = k;
        index++;
    }
    if (g_bootInput != nullptr) {
        g_bootInput->setValue(report, 8);
        g_bootInput->notify();
    }
}
