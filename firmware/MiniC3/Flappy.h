#ifndef FLAPPY_H
#define FLAPPY_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "GameKeys.h"
#include "PolledGame.h"

// Mini C3 v1.10.0: a little bird that flies between brick columns.
//
//   A or ↑  flap          ↓  pause (↑ ↓ choose, A ok)
//   end     A play again · ← back to the menu
class Flappy : public PolledGame {
public:
    void begin() override;
    void start(uint32_t now) override;
    bool active() const override { return phase != P_OFF; }
    void pause() override;
    void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) override;
    void draw(Adafruit_GFX& d, uint32_t now) override;
    uint32_t record() const override { return best; }
    uint16_t frameMs() const override { return 16; }
    const char* title() const override { return "FLAPPY"; }
    void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) override;

    // the bird, also used by the menu demo (wingUp: wing frame)
    static void drawBird(Adafruit_GFX& d, int x, int y, bool wingUp, bool dead = false);
    static void drawColumn(Adafruit_GFX& d, int x, int gapTop, int gapBottom, int ground, int sky = -1);

private:
    enum Phase : uint8_t { P_OFF, P_READY, P_PLAY, P_DEAD, P_OVER, P_PAUSE };
    struct Pipe { float x; int8_t gapY; bool scored; bool alive; };
    struct Feather { float x, y, vx, vy; uint16_t life; bool alive; };

    static const int MAX_PIPES = 4;
    static const int MAX_FEATHERS = 10;

    Phase phase = P_OFF;
    Phase pausedFrom = P_PLAY;
    uint32_t phaseAt = 0;
    uint32_t lastFrame = 0;

    float y = 28, vy = 0;
    uint32_t flapAt = 0;
    Pipe pipes[MAX_PIPES];
    Feather feathers[MAX_FEATHERS];
    float scroll = 0;          // ground / background
    uint32_t score = 0, best = 0;
    bool newRecord = false;
    int pauseSel = 0;
    uint32_t shakeUntil = 0;
    GameKeys prev = {true, true, true, true, true};

    uint32_t ledUntil = 0;
    bool ledOn = false;

    float speed() const;
    int gap() const;
    void flap(uint32_t now);
    void die(uint32_t now);
    void led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms, uint32_t now);
    void ledOff();
    void saveRecord();
};

#endif
