#ifndef GAME_UI_H
#define GAME_UI_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "GameKeys.h"

// Mini C3 v1.11.0: pieces shared by the games (so each one stays small).
namespace GameUi {

// Keys that went down since the last frame
GameKeys edges(const GameKeys& now, GameKeys& prev);

// Board LED (the C3-Zero LED takes RGB; rgbLedWrite sends GRB)
struct Led {
    uint32_t until = 0;
    bool on = false;
    void set(uint8_t r, uint8_t g, uint8_t b, uint16_t ms, uint32_t now);
    void tick(uint32_t now);
    void off();
};

// Records in the NVS namespace "minic3_game"
uint32_t loadRecord(const char* key, uint32_t def = 0);
void saveRecord(const char* key, uint32_t value);

// "PAUSA" box with Seguir / Salir (sel 0 / 1)
void drawPause(Adafruit_GFX& d, int sel);
// Pause keys: ↑ ↓ choose, A confirms. Returns 0 nothing, 1 continue, 2 leave.
int pauseInput(const GameKeys& e, int& sel);

// End box: title, big score, record line, "A otra vez ← menu"
void drawOver(Adafruit_GFX& d, const char* title, const String& score, const String& recordLine,
              bool newRecord, uint32_t now, uint32_t since);
// End keys (after a short moment): returns 0 nothing, 1 play again, 2 menu
int overInput(const GameKeys& e, uint32_t now, uint32_t since);

void centerText(Adafruit_GFX& d, const String& s, int y, int size = 1);

// 3x5 digits for small tiles (returns the width drawn)
int tinyNumber(Adafruit_GFX& d, int x, int y, const String& s, uint16_t color);
int tinyWidth(const String& s);

}   // namespace GameUi

#endif
