#ifndef PONG_H
#define PONG_H

#include "GameUi.h"
#include "PolledGame.h"

// Mini C3 v1.13.0: Pong against the CPU. ↑ ↓ move your paddle (left), A
// pauses. Matches to 7. Each match you win makes the next CPU faster and
// sharper; the record is the longest winning streak.
class Pong : public PolledGame {
public:
    void begin() override { best = GameUi::loadRecord("png_hi"); }
    void start(uint32_t now) override;
    bool active() const override { return phase != P_OFF; }
    void pause() override;
    void frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) override;
    void draw(Adafruit_GFX& d, uint32_t now) override;
    uint32_t record() const override { return best; }
    uint16_t frameMs() const override { return 16; }
    const char* title() const override { return "PONG"; }
    void drawDemo(Adafruit_GFX& d, uint32_t now, int ox) override;
    String recordText() const override {
        return best > 0 ? "Racha record " + String(best) : String("Sin record");
    }
    // ↑ moves the paddle: held for a long time, it must not turn it off
    bool holdsUp() const override { return phase == P_SERVE || phase == P_PLAY || phase == P_POINT; }

    static const int WIN_POINTS = 7;

private:
    enum Phase : uint8_t { P_OFF, P_SERVE, P_PLAY, P_POINT, P_PAUSE, P_OVER };
    Phase phase = P_OFF;
    Phase pausedFrom = P_PLAY;
    uint32_t phaseAt = 0;
    uint32_t lastFrame = 0;
    int pauseSel = 0;

    float py = 26, cy = 26;          // paddles (top y)
    float bx = 63, by = 31, bvx = 0, bvy = 0, speed = 0;
    float cpuAim = 0;                // where the CPU wants the ball on its paddle
    int you = 0, cpu = 0;
    int serveTo = 1;                 // 1 → CPU side, -1 → your side
    int streak = 0, match = 1;
    uint32_t best = 0;
    bool won = false, newRecord = false;
    GameKeys prev = {true, true, true, true, true};
    GameUi::Led led;

    void newMatch(uint32_t now);
    void serve(uint32_t now);
    void point(bool yours, uint32_t now);
    void bounceOff(float paddleY, int dir);
    float predictY() const;
    float cpuSpeed() const;
};

#endif
