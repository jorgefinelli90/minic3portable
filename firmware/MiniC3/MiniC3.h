#ifndef MINI_C3_H
#define MINI_C3_H

// ---------------------------------------------------------------------------
//  Mini C3 handheld — board definition
//  Waveshare ESP32-C3-Zero + OLED 1.3" SH1106 (I2C) + 4 arrows + TTP223 touch
// ---------------------------------------------------------------------------

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifndef FW_VERSION
#define FW_VERSION "1.0.0"
#endif

// OTA password (ArduinoOTA + web page /ota). Set in platformio.ini so the
// espota upload uses the same value.
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "minic3"
#endif
#define OTA_WEB_USER "admin"

#define DEVICE_HOSTNAME "minic3"     // -> http://minic3.local
#define SETUP_AP_SSID   "MiniC3-Setup"
#define SETUP_AP_PASS   "minic3setup"

// --- Pins (see the wiring diagram) ---
#define PIN_SDA      8
#define PIN_SCL      9
#define PIN_UP       3   // to GND, INPUT_PULLUP
#define PIN_DOWN     4
#define PIN_LEFT     5
#define PIN_RIGHT    6
#define PIN_TOUCH    7   // TTP223 OUT, active HIGH
#define PIN_BATTERY  0   // 100k/100k divider from battery + (optional)
#define MINI_RGB_LED_PIN 10  // on-board WS2812
// The C3-Zero LED takes the colors in RGB order (rgbLedWrite sends GRB)
#define MINI_LED_RGB_ORDER 1

#define OLED_I2C_ADDR 0x3C
// 1 = image rotated 180° (OLED mounted with its pins at the bottom, as in the case)
#ifndef OLED_FLIP
#define OLED_FLIP 1
#endif

// ---------------------------------------------------------------------------
//  Shared-state lock
//  The background sync task replaces AppState.flight while the main loop draws
//  it; both sides take this recursive mutex.
// ---------------------------------------------------------------------------
extern SemaphoreHandle_t gStateMutex;

struct StateLock {
    bool locked = false;
    explicit StateLock(TickType_t waitTicks = portMAX_DELAY) {
        if (gStateMutex) locked = (xSemaphoreTakeRecursive(gStateMutex, waitTicks) == pdTRUE);
    }
    ~StateLock() {
        if (locked) xSemaphoreGiveRecursive(gStateMutex);
    }
    StateLock(const StateLock&) = delete;
    StateLock& operator=(const StateLock&) = delete;
};

// Set while a firmware update is being written (pauses background syncs)
extern volatile bool gOtaInProgress;

#endif
