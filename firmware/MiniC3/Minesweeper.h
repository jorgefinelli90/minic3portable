#ifndef MINESWEEPER_H
#define MINESWEEPER_H

#include "GameUi.h"
#include "PolledGame.h"

// Mini C3 v1.11.0: Buscaminas, 16 x 7 with 16 mines.
//   arrows move (they repeat while held) · A (tap) opens
//   A held half a second: flag · A held 1.5 s: pause
//   A on an open number whose flags are all there opens the cells around
// The first cell you open never has a mine. The record is the best time.
class Minesweeper : public PolledGame {
public:
    void begin() override { best = GameUi::loadRecord("min_hi"); }
    void start(uint32_t now) override;
    bool active() const override { return phase != P_OFF; }
    void pause() override;
    void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) override;
    void draw(Adafruit_GFX& d, uint32_t now) override;
    uint32_t record() const override { return best; }
    const char* title() const override { return "BUSCAMINAS"; }
    void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) override;
    String recordText() const override {
        return best > 0 ? "Mejor " + String(best) + " s" : String("Sin record");
    }

    static const int COLS = 16, ROWS = 7, MINES = 16;

private:
    enum Phase : uint8_t { P_OFF, P_PLAY, P_PAUSE, P_BOOM, P_OVER };
    enum : uint8_t { MINE = 0x10, OPEN = 0x20, FLAG = 0x40 };   // low bits: mines around

    Phase phase = P_OFF;
    uint32_t phaseAt = 0;
    int pauseSel = 0;
    uint8_t cell[ROWS][COLS];
    bool placed = false;
    int cx = 0, cy = 0;
    int opened = 0, flags = 0;
    uint32_t startAt = 0, elapsed = 0;
    bool won = false;
    uint32_t best = 0;
    bool newRecord = false;
    int boomX = -1, boomY = -1;

    GameKeys prev = {true, true, true, true, true};
    uint32_t aSince = 0;
    uint8_t aStage = 0;   // 0 nothing yet, 1 flagged, 2 paused
    int8_t repDir = -1;
    uint32_t repAt = 0;
    GameUi::Led led;

    void placeMines(int sx, int sy);
    void open(int x, int y, uint32_t now);
    void chord(int x, int y, uint32_t now);
    void lose(int x, int y, uint32_t now);
    void checkWin(uint32_t now);
    uint32_t seconds(uint32_t now) const;
};

#endif
