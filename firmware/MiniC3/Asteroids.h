#ifndef ASTEROIDS_H
#define ASTEROIDS_H

#include "GameUi.h"
#include "PolledGame.h"

// Mini C3 v1.13.0: Asteroides. ← → turn the ship, ↑ thrusts, A fires (held =
// auto fire), ↓ pauses. Big rocks split in two medium ones, those in two small
// ones. The screen wraps around at the edges. Extra life every 5000 points.
class Asteroids : public PolledGame {
public:
    void begin() override { best = GameUi::loadRecord("ast_hi"); }
    void start(uint32_t now) override;
    bool active() const override { return phase != P_OFF; }
    void pause() override;
    void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) override;
    void draw(Adafruit_GFX& d, uint32_t now) override;
    uint32_t record() const override { return best; }
    uint16_t frameMs() const override { return 20; }
    const char* title() const override { return "ASTEROIDES"; }
    void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) override;
    // ↑ is the thrust: held for a long time, it must not turn the console off
    bool holdsUp() const override { return phase == P_WAVE || phase == P_PLAY || phase == P_DEAD; }

    static const int MAX_ROCKS = 26;
    static const int MAX_SHOTS = 4;

private:
    enum Phase : uint8_t { P_OFF, P_WAVE, P_PLAY, P_DEAD, P_PAUSE, P_OVER };
    struct Rock { float x, y, vx, vy, a, va; uint8_t size; uint8_t rad[8]; };
    struct Shot { float x, y, vx, vy; uint32_t until; };
    struct Spark { float x, y, vx, vy; uint32_t until; };

    Phase phase = P_OFF;
    Phase pausedFrom = P_PLAY;
    uint32_t phaseAt = 0;
    uint32_t lastFrame = 0;
    int pauseSel = 0;

    float sx = 64, sy = 32, svx = 0, svy = 0, sa = 0;   // ship (angle 0 = up)
    bool thrusting = false;
    uint32_t safeUntil = 0;        // blinking, rocks pass through it
    uint32_t lastShot = 0;
    Rock rocks[MAX_ROCKS];
    Shot shots[MAX_SHOTS];
    Spark sparks[12];

    int lives = 3, wave = 1;
    uint32_t score = 0, best = 0, nextLife = 5000;
    bool newRecord = false;
    GameKeys prev = {true, true, true, true, true};
    GameUi::Led led;

    void newWave(uint32_t now);
    void addRock(float x, float y, uint8_t size, float vx, float vy);
    void hitRock(int i, uint32_t now);
    void addSparks(float x, float y, int n, float speed, uint32_t now);
    void shipHit(uint32_t now);
    void gameOver(uint32_t now);
    void respawn(uint32_t now);
    static float radius(uint8_t size) { return size == 3 ? 8.0f : size == 2 ? 5.0f : 3.0f; }
    static void drawRock(Adafruit_GFX& d, const Rock& r, float x, float y);
    static void drawShip(Adafruit_GFX& d, float x, float y, float a, bool flame);
};

#endif
