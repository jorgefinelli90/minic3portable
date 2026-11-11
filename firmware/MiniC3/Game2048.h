#ifndef GAME_2048_H
#define GAME_2048_H

#include "GameUi.h"
#include "PolledGame.h"

// Mini C3 v1.11.0: 2048. Arrows slide every tile, equal tiles join.
// A pauses. 1024 and up are shown as 1k, 2k...
class Game2048 : public PolledGame {
public:
    void begin() override { best = GameUi::loadRecord("g48_hi"); }
    void start(uint32_t now) override;
    bool active() const override { return phase != P_OFF; }
    void pause() override;
    void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) override;
    void draw(Adafruit_GFX& d, uint32_t now) override;
    uint32_t record() const override { return best; }
    const char* title() const override { return "2048"; }
    void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) override;

    static void drawTile(Adafruit_GFX& d, int x, int y, int size, uint16_t v);

private:
    enum Phase : uint8_t { P_OFF, P_PLAY, P_SLIDE, P_PAUSE, P_OVER };
    struct Move { int8_t fr, fc, tr, tc; uint16_t v; };

    Phase phase = P_OFF;
    uint32_t phaseAt = 0;
    int pauseSel = 0;
    uint16_t grid[4][4];
    uint16_t after[4][4];       // result of the move being animated
    Move moves[16];
    int moveCount = 0;
    int8_t newR = -1, newC = -1;
    uint32_t newAt = 0;
    uint32_t score = 0, best = 0;
    bool newRecord = false;
    bool won = false;
    uint32_t toastUntil = 0;
    GameKeys prev = {true, true, true, true, true};
    GameUi::Led led;

    bool slide(int dir, uint32_t now);   // 0 up 1 down 2 left 3 right
    void addTile(uint16_t (*g)[4], uint32_t now);
    bool canMove() const;
    void finish(uint32_t now);
};

#endif
