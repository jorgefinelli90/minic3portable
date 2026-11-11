#ifndef INVADERS_H
#define INVADERS_H

#include "GameUi.h"
#include "PolledGame.h"

// Mini C3 v1.11.0: Invasores (original drawings). ← → move the ship,
// A or ↑ fire, ↓ pause. The formation speeds up as it shrinks; shelters wear
// away; now and then a saucer crosses the top.
class Invaders : public PolledGame {
public:
    void begin() override { best = GameUi::loadRecord("inv_hi"); }
    void start(uint32_t now) override;
    bool active() const override { return phase != P_OFF; }
    void pause() override;
    void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) override;
    void draw(Adafruit_GFX& d, uint32_t now) override;
    uint32_t record() const override { return best; }
    uint16_t frameMs() const override { return 20; }
    const char* title() const override { return "INVASORES"; }
    void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) override;

    static const int ROWS = 4, COLS = 7;

private:
    enum Phase : uint8_t { P_OFF, P_WAVE, P_PLAY, P_HIT, P_PAUSE, P_OVER };
    struct Shot { float x, y; bool alive; };
    struct Boom { int16_t x, y; uint32_t at; };

    Phase phase = P_OFF;
    uint32_t phaseAt = 0;
    uint32_t lastFrame = 0;
    int pauseSel = 0;

    bool alive[ROWS][COLS];
    int aliveCount = 0;
    int fx = 0, fy = 0, fdir = 1;
    uint32_t marchAt = 0;
    bool frameAlt = false;

    float px = 59;
    Shot bullet;
    Shot bombs[3];
    uint32_t bombAt = 0;
    uint16_t bunker[3][5];
    Boom booms[4];
    float ufoX = -20;
    bool ufoOn = false;
    int8_t ufoDir = 1;
    uint32_t ufoAt = 0;

    int lives = 3, wave = 1;
    uint32_t score = 0, best = 0;
    bool newRecord = false;
    GameKeys prev = {true, true, true, true, true};
    GameUi::Led led;

    void newWave(uint32_t now);
    void addBoom(int x, int y, uint32_t now);
    bool hitBunker(float x, float y, int h);
    void gameOver(uint32_t now);
};

#endif
