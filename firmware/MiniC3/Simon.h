#ifndef SIMON_H
#define SIMON_H

#include "GameUi.h"
#include "PolledGame.h"

// Mini C3 v1.11.0: Simón. It shows a sequence of arrows (each one also lights
// the board LED in its color: ↑ green, → red, ↓ yellow, ← blue) and you repeat
// it with the arrows. One more each round, a bit faster. A pauses.
class Simon : public PolledGame {
public:
    void begin() override { best = GameUi::loadRecord("sim_hi"); }
    void start(uint32_t now) override;
    bool active() const override { return phase != P_OFF; }
    void pause() override;
    void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) override;
    void draw(Adafruit_GFX& d, uint32_t now) override;
    uint32_t record() const override { return best; }
    const char* title() const override { return "SIMON"; }
    void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) override;

    static const int MAX_LEN = 64;

private:
    enum Phase : uint8_t { P_OFF, P_SHOW, P_INPUT, P_GOOD, P_FAIL, P_PAUSE, P_OVER };
    Phase phase = P_OFF;
    Phase pausedFrom = P_SHOW;
    uint32_t phaseAt = 0;
    int pauseSel = 0;

    uint8_t seq[MAX_LEN];
    int len = 0;
    int pos = 0;             // shown / typed so far
    int lit = -1;            // pad lit right now
    uint32_t litUntil = 0;
    uint32_t best = 0;
    bool newRecord = false;
    GameKeys prev = {true, true, true, true, true};
    GameUi::Led led;

    uint32_t onMs() const;
    void light(int pad, uint32_t ms, uint32_t now);
    static void drawPad(Adafruit_GFX& d, int pad, bool on, int ox, int oy);
};

#endif
