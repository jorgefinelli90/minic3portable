#ifndef POLLED_GAME_H
#define POLLED_GAME_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "GameKeys.h"

// Mini C3: a game that reads the keys straight from the hardware every frame
// (Tetris, Flappy and the ones added in v1.11.0). The games screen keeps a
// list of them and runs whichever is active.
class PolledGame {
public:
    virtual ~PolledGame() {}
    virtual void begin() = 0;                    // loads the record
    virtual void start(uint32_t now) = 0;
    virtual bool active() const = 0;
    virtual void pause() = 0;                    // from outside (message, sleep)
    virtual void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) = 0;
    virtual void draw(Adafruit_GFX& d, uint32_t now) = 0;
    virtual uint32_t record() const = 0;
    virtual uint16_t frameMs() const { return 25; }

    // games menu: name and a small animation under it (y 30..52)
    virtual const char* title() const = 0;
    virtual void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {}
    // v1.13.0: true while ↑ is a game key that is held down for a long time
    // (Pong, Asteroides). Then holding ↑ 3 s does not turn the console off.
    virtual bool holdsUp() const { return false; }
    // "Record 1234" by default (Buscaminas shows a time, Simon a length)
    virtual String recordText() const {
        return record() > 0 ? "Record " + String(record()) : String("Sin record");
    }
};

#endif
