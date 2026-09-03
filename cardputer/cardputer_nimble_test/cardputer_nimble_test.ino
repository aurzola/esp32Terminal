#include <Arduino.h>
#include <M5Cardputer.h>

void setup(void)
{
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextColor(GREEN);
    M5Cardputer.Display.drawString("TEST",
                                   M5Cardputer.Display.width() / 2,
                                   M5Cardputer.Display.height() / 2);
    Serial.begin(115200);
    Serial.printf("no-ble ok\n");
}

void loop(void)
{
    M5Cardputer.update();
    delay(10);
}
