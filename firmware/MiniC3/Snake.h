#ifndef SNAKE_H
#define SNAKE_H

#include "GameUi.h"
#include "PolledGame.h"

// Mini C3 v1.11.0: Viborita. Arrows turn, A pauses. It speeds up as it grows;
// every 5 apples a big one appears for a few seconds (worth more).
class Snake : public PolledGame {
public:
    void begin() override { best = GameUi::loadRecord("snk_hi"); }
    void start(uint32_t now) override;
    bool active() const override { return phase != P_OFF; }
    void pause() override;
    void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) override;
    void draw(Adafruit_GFX& d, uint32_t now) override;
    uint32_t record() const override { return best; }
    uint16_t frameMs() const override { return 20; }
    const char* title() const override { return "VIBORITA"; }
    void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) override;

    static const int COLS = 31, ROWS = 13;

private:
    enum Phase : uint8_t { P_OFF, P_READY, P_PLAY, P_PAUSE, P_DEAD, P_OVER };
    Phase phase = P_OFF;
    uint32_t phaseAt = 0;
    int pauseSel = 0;

    uint8_t bx[COLS * ROWS], by[COLS * ROWS];   // body, [0] = head
    int len = 0;
    int8_t dx = 1, dy = 0;
    int8_t queue[2][2];
    int queued = 0;
    uint32_t stepAt = 0;
    int8_t foodX = 0, foodY = 0;
    int8_t bonusX = -1, bonusY = -1;
    uint32_t bonusUntil = 0;
    int eaten = 0;
    uint32_t score = 0, best = 0;
    bool newRecord = false;
    GameKeys prev = {true, true, true, true, true};
    GameUi::Led led;

    bool occupied(int x, int y, int from = 0) const;
    void placeFood();
    void step(uint32_t now);
    void die(uint32_t now);
    void turn(int8_t ndx, int8_t ndy);
    void drawBody(Adafruit_GFX& d, int x0, int y0, const uint8_t* xs, const uint8_t* ys, int n, int8_t hdx, int8_t hdy);
};

#endif
