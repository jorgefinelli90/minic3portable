#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <Arduino.h>

// Firmware updates over WiFi, two ways:
//  1. PlatformIO / Arduino IDE (espota, port 3232)  -> pio run -e minic3-ota -t upload
//  2. Browser: http://minic3.local/ota  (upload firmware.bin)
// Both share the on-screen progress below. The web upload itself is handled
// by WebServerService, which calls the ui* hooks.
class OtaService {
public:
    void begin();          // call once WiFi is connected
    void handle();         // call every loop()
    bool isReady() const { return ready; }

    static void uiStart(const String& source);
    static void uiProgress(size_t done, size_t total);
    static void uiEnd(bool ok, const String& message);

private:
    bool ready = false;
};

#endif
