#ifndef TRUCO_SERVICE_H
#define TRUCO_SERVICE_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "HardwareService.h"

class MiniOled;

// Mini C3 v1.7.0: truco score pad ("anotador"), drawn with matchsticks:
// squares of 5, malas on top and buenas below.
//
//   ←  tap: +1 ellos      held: -1 ellos
//   →  tap: +1 nosotros   held: -1 nosotros
//   A  held: new game (asks for A again) · tap at 0-0: game to 15 or 30
//   ↑ / ↓ other screens as usual
//
// The score is saved, so it survives turning it off or restarting.
class TrucoService {
public:
    void begin();

    // Keys on the truco screen. Returns true when it used the key.
    bool handleInput(InputEvent e);

    void draw(Adafruit_GFX& d);
    // On the truco screen: animations (a stick being drawn, the win...)
    void tick(MiniOled& oled);

    // A game is going on (auto cycle must not leave the screen)
    bool inProgress() const { return (ellos > 0 || nosotros > 0) && winner() < 0; }

    // A short message on the pad (e.g. "WiFi apagado: ahorro")
    void notice(const String& s) { showToast(s, 2000); }

private:
    uint8_t ellos = 0;
    uint8_t nosotros = 0;
    uint8_t target = 30;            // 15 or 30

    // last change, animated: side 0 ellos / 1 nosotros, +1 drawn / -1 erased
    int animSide = -1;
    int animStick = 0;              // 1-based point number
    int animDir = 0;
    uint32_t animAt = 0;

    uint32_t confirmUntil = 0;      // "Nueva partida?" waiting for A
    String toast = "";
    uint32_t toastUntil = 0;
    uint32_t winAt = 0;
    bool dirty = true;
    uint32_t lastDraw = 0;

    uint32_t ledUntil = 0;
    bool ledOn = false;

    int winner() const;             // -1 none, 0 ellos, 1 nosotros
    void add(int side, int delta);
    void save();
    void showToast(const String& s, uint32_t ms = 1400);
    void led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms);

    void drawSide(Adafruit_GFX& d, int side, uint32_t now);
    void drawStick(Adafruit_GFX& d, int x, int y, int k, float f);
};

#endif
