#include "OtaService.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "DisplayService.h"
#include "MiniC3.h"

extern DisplayService displayService;

static int lastShownPercent = -1;

void OtaService::begin() {
    if (WiFi.status() != WL_CONNECTED) return;

    ArduinoOTA.setHostname(DEVICE_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setMdnsEnabled(false);   // mDNS is already started by the web server

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "datos";
        uiStart("PC: " + type);
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        uiProgress(progress, total);
    });
    ArduinoOTA.onEnd([]() {
        uiEnd(true, "Reiniciando...");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        String msg;
        switch (error) {
            case OTA_AUTH_ERROR:    msg = "Clave incorrecta"; break;
            case OTA_BEGIN_ERROR:   msg = "No hay lugar"; break;
            case OTA_CONNECT_ERROR: msg = "Error de conexion"; break;
            case OTA_RECEIVE_ERROR: msg = "Error recibiendo"; break;
            case OTA_END_ERROR:     msg = "Error al cerrar"; break;
            default:                msg = "Error"; break;
        }
        uiEnd(false, msg);
    });

    ArduinoOTA.begin();
    MDNS.enableArduino(3232, true);   // shows up as a network port in Arduino IDE

    ready = true;
    Serial.printf("OTA: listo en %s.local / %s (puerto 3232)\n", DEVICE_HOSTNAME, WiFi.localIP().toString().c_str());
}

void OtaService::handle() {
    if (ready) ArduinoOTA.handle();
}

void OtaService::uiStart(const String& source) {
    gOtaInProgress = true;
    lastShownPercent = -1;
    Serial.println("OTA: inicio (" + source + ")");
    rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 40);   // blue while writing
    StateLock lock;
    displayService.setContrast(false);
    displayService.drawOtaProgress(0, "Actualizando", source);
}

void OtaService::uiProgress(size_t done, size_t total) {
    int percent = (total > 0) ? (int)((done * 100ULL) / total) : 0;
    if (percent == lastShownPercent) return;
    if (lastShownPercent >= 0 && percent < 100 && percent - lastShownPercent < 2) return;
    lastShownPercent = percent;
    StateLock lock;
    displayService.drawOtaProgress(percent, "Actualizando", "No apagues");
}

void OtaService::uiEnd(bool ok, const String& message) {
    rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);
    Serial.println(String("OTA: ") + (ok ? "OK " : "ERROR ") + message);
    {
        StateLock lock;
        if (ok) displayService.drawOtaProgress(100, "Actualizado!", message);
        else displayService.drawInfoScreen(nullptr, "Error OTA");
        if (!ok) {
            displayService.display.setTextSize(1);
            displayService.display.setCursor(8, 50);
            displayService.display.print(message);
            displayService.display.display();
        }
    }
    if (!ok) {
        delay(2500);
        gOtaInProgress = false;
    }
}
