#ifndef TETRIS_H
#define TETRIS_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "GameKeys.h"
#include "PolledGame.h"

// Mini C3 v1.10.0: falling blocks. A 10 x 16 well (the screen is 64 px tall,
// 4 px per block), so the whole well fits without turning the console.
//
//   ← / →   move (it repeats while held)   ↓  soft drop
//   ↑       hard drop                      A  rotate
//   A held  pause: ↑ ↓ choose, A ok
//   end     A play again · ← back to the menu
class Tetris : public PolledGame {
public:
    static const int COLS = 10;
    static const int ROWS = 16;

    void begin() override;
    void start(uint32_t now) override;
    bool active() const override { return phase != P_OFF; }
    void pause() override;
    void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) override;
    void draw(Adafruit_GFX& d, uint32_t now) override;
    uint32_t record() const override { return best; }
    uint16_t frameMs() const override { return 25; }
    const char* title() const override { return "TETRIS"; }
    void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) override;

    // the 7 pieces, 4 rotations, 4x4 masks (bit 15 = row 0 col 0)
    static uint16_t shape(uint8_t type, uint8_t rot);

private:
    enum Phase : uint8_t { P_OFF, P_PLAY, P_CLEAR, P_PAUSE, P_FILL, P_OVER };

    Phase phase = P_OFF;
    Phase pausedFrom = P_PLAY;
    uint32_t phaseAt = 0;
    uint8_t board[ROWS][COLS];

    uint8_t type = 0, rot = 0;
    int8_t px = 3, py = 0;
    uint8_t nextType = 0;
    uint8_t bag[7];
    uint8_t bagPos = 7;

    uint32_t fallAt = 0;
    uint32_t lockAt = 0;
    uint8_t lockResets = 0;
    uint16_t clearMask = 0;

    GameKeys prev = {true, true, true, true, true};
    int8_t dasDir = 0;
    uint32_t dasAt = 0;
    uint32_t aSince = 0;
    bool aHoldUsed = false;
    int pauseSel = 0;

    uint32_t score = 0, best = 0;
    uint16_t lines = 0;
    uint8_t level = 1;
    bool newRecord = false;

    uint32_t ledUntil = 0;
    bool ledOn = false;

    uint8_t draw7();
    bool collides(uint8_t t, uint8_t r, int x, int y) const;
    bool spawn(uint32_t now);
    bool tryMove(int dx, int dy, uint32_t now);
    void rotate(uint32_t now);
    void hardDrop(uint32_t now);
    void lockPiece(uint32_t now);
    void finishClear(uint32_t now);
    void gameOver(uint32_t now);
    uint32_t fallInterval() const;
    int ghostY() const;
    void led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms, uint32_t now);
    void ledOff();
    void saveRecord();
};

#endif
