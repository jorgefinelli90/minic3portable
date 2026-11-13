#include "Pong.h"

#include <Adafruit_SH110X.h>

static const int PAD_H = 12, PAD_W = 2;
static const int YOU_X = 3, CPU_X = 123;
static const int BALL = 2;
static const float YOU_V = 100.0f;       // your paddle, px/s
static const float MAX_BALL = 150.0f;

void Pong::start(uint32_t now) {
    streak = 0;
    match = 1;
    newRecord = false;
    prev = {true, true, true, true, true};
    newMatch(now);
}

void Pong::newMatch(uint32_t now) {
    you = cpu = 0;
    won = false;
    py = cy = (64 - PAD_H) / 2;
    serveTo = random(0, 2) ? 1 : -1;
    serve(now);
    phaseAt = now + 700;   // a longer wait on the first serve: "PARTIDO n"
}

// The CPU gets better with every match you win in a row
float Pong::cpuSpeed() const {
    return 50.0f + 9.0f * min(streak, 7);
}

void Pong::serve(uint32_t now) {
    bx = 63;
    by = random(18, 44);
    speed = 58.0f + 4.0f * min(streak, 6);
    float ang = random(-35, 36) * 0.0174533f;
    bvx = cosf(ang) * speed * serveTo;
    bvy = sinf(ang) * speed;
    cpuAim = 0;
    phase = P_SERVE;
    phaseAt = now;
    lastFrame = now;
}

void Pong::pause() {
    if (phase != P_SERVE && phase != P_PLAY && phase != P_POINT) return;
    pausedFrom = phase;
    phase = P_PAUSE;
    pauseSel = 0;
    led.off();
}

// Where the ball will reach the CPU's paddle, bouncing off top and bottom
float Pong::predictY() const {
    if (bvx <= 0) return 32;
    float t = (CPU_X - BALL - bx) / bvx;
    float y = by + bvy * t;
    const float top = 0, bottom = 64 - BALL;
    for (int i = 0; i < 8 && (y < top || y > bottom); i++) {
        if (y < top) y = -y;
        if (y > bottom) y = 2 * bottom - y;
    }
    return y;
}

// The ball hits a paddle: it goes back with an angle that depends on where it
// hit (the ends send it steeper) and a bit faster.
void Pong::bounceOff(float paddleY, int dir) {
    float off = ((by + BALL / 2.0f) - (paddleY + PAD_H / 2.0f)) / (PAD_H / 2.0f + 1);
    off = constrain(off, -1.0f, 1.0f);
    float ang = off * 1.0f;   // up to ~57°
    speed = min(speed * 1.06f, MAX_BALL);
    bvx = cosf(ang) * speed * dir;
    bvy = sinf(ang) * speed;
}

void Pong::point(bool yours, uint32_t now) {
    if (yours) {
        you++;
        led.set(0, 40, 0, 350, now);
    } else {
        cpu++;
        led.set(45, 0, 0, 350, now);
    }
    serveTo = yours ? 1 : -1;   // it goes towards whoever won the point
    phase = P_POINT;
    phaseAt = now;

    if (you >= WIN_POINTS || cpu >= WIN_POINTS) {
        won = you > cpu;
        if (won) {
            streak++;
            newRecord = (uint32_t)streak > best;
            if (newRecord) {
                best = streak;
                GameUi::saveRecord("png_hi", best);
            }
            led.set(0, 45, 10, 700, now);
        } else {
            newRecord = false;
        }
        phase = P_OVER;
        phaseAt = now;
    }
}

void Pong::frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) {
    GameKeys e = GameUi::edges(k, prev);
    led.tick(now);
    uint32_t gap = now - lastFrame;
    float dt = gap > 200 ? 0.0f : min(gap, (uint32_t)40) / 1000.0f;
    lastFrame = now;

    switch (phase) {
        case P_PAUSE: {
            int r = GameUi::pauseInput(e, pauseSel);
            if (r == 1) {
                phase = pausedFrom;
                lastFrame = now;
            } else if (r == 2) {
                led.off();
                phase = P_OFF;
                return;
            }
            draw(d, now);
            return;
        }
        case P_OVER: {
            int r = GameUi::overInput(e, now, phaseAt);
            if (r == 1) {
                // won: the next, harder match; lost: from the start
                if (!won) streak = 0;
                match = won ? match + 1 : 1;
                newRecord = false;
                newMatch(now);
            } else if (r == 2) {
                led.off();
                phase = P_OFF;
                return;
            }
            draw(d, now);
            return;
        }
        default:
            break;
    }

    if (e.a) {
        pause();
        draw(d, now);
        return;
    }

    // your paddle
    if (k.up) py -= YOU_V * dt;
    if (k.down) py += YOU_V * dt;
    py = constrain(py, 0.0f, (float)(64 - PAD_H));

    // the CPU: follows where the ball will arrive (with some error), and only
    // once the ball is coming its way and past a point that depends on the level
    float reactX = max(24.0f, 72.0f - streak * 7.0f);
    float target;
    if (bvx > 0 && bx > reactX && phase == P_PLAY) target = predictY() + cpuAim - PAD_H / 2.0f + BALL / 2.0f;
    else target = (64 - PAD_H) / 2.0f;
    float step = cpuSpeed() * dt;
    if (fabsf(target - cy) <= step) cy = target;
    else cy += (target > cy) ? step : -step;
    cy = constrain(cy, 0.0f, (float)(64 - PAD_H));

    if (phase == P_SERVE) {
        if ((int32_t)(now - phaseAt) >= 800) phase = P_PLAY;
        draw(d, now);
        return;
    }
    if (phase == P_POINT) {
        if (now - phaseAt >= 900) serve(now);
        draw(d, now);
        return;
    }

    // the ball
    bx += bvx * dt;
    by += bvy * dt;
    if (by < 0) {
        by = -by;
        bvy = fabsf(bvy);
    }
    if (by > 64 - BALL) {
        by = 2 * (64 - BALL) - by;
        bvy = -fabsf(bvy);
    }

    // your paddle
    if (bvx < 0 && bx <= YOU_X + PAD_W && bx + BALL >= YOU_X && by + BALL >= py && by <= py + PAD_H) {
        bx = YOU_X + PAD_W;
        bounceOff(py, 1);
        // the CPU will miss by up to this much (less as the level goes up)
        // (past the end of its paddle it misses: ~30 % of the time on the first
        // match, never by aim from the 7th on; then only its speed limits it)
        float err = max(0.0f, 4.0f - streak * 0.6f);
        cpuAim = random(-100, 101) / 100.0f * (PAD_H / 2.0f + err);
        led.set(12, 12, 12, 25, now);
    }
    // the CPU's paddle
    if (bvx > 0 && bx + BALL >= CPU_X && bx <= CPU_X + PAD_W && by + BALL >= cy && by <= cy + PAD_H) {
        bx = CPU_X - BALL;
        bounceOff(cy, -1);
    }

    if (bx < -4) point(false, now);
    else if (bx > 128 + 2) point(true, now);

    draw(d, now);
}

void Pong::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    // dashed net
    for (int y = 1; y < 64; y += 4) d.drawFastVLine(63, y, 2, SH110X_WHITE);

    // score: you on the left, the CPU on the right
    d.setCursor(63 - 10 - 6 * (you >= 10 ? 2 : 1), 2);
    d.print(you);
    d.setCursor(63 + 10, 2);
    d.print(cpu);

    d.fillRect(YOU_X, (int)py, PAD_W, PAD_H, SH110X_WHITE);
    d.fillRect(CPU_X, (int)cy, PAD_W, PAD_H, SH110X_WHITE);

    bool ballOn = phase == P_PLAY || (phase == P_SERVE && (now / 150) % 2 == 0) || phase == P_PAUSE;
    if (ballOn) d.fillRect((int)bx, (int)by, BALL, BALL, SH110X_WHITE);

    if (phase == P_SERVE && you == 0 && cpu == 0) {
        String t = "PARTIDO " + String(match);
        int w = t.length() * 6 + 8;
        d.fillRect(64 - w / 2, 24, w, 18, SH110X_BLACK);
        GameUi::centerText(d, t, 25);
        GameUi::centerText(d, "a " + String(WIN_POINTS), 34);
    }
    if (phase == P_PAUSE) GameUi::drawPause(d, pauseSel);
    if (phase == P_OVER) {
        String rec = won ? "Racha " + String(streak) : "Racha record " + String(best);
        GameUi::drawOver(d, won ? "GANASTE!" : "PERDISTE", String(you) + "-" + String(cpu), rec, newRecord, now, phaseAt);
    }
}

void Pong::drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {
    // a rally that never ends: the ball bounces and both paddles follow it
    auto tri = [](uint32_t t, uint32_t period) {
        float p = (t % period) / (float)period;
        return p < 0.5f ? p * 2 : 2 - p * 2;
    };
    int x = 24 + (int)(tri(now, 1800) * 78);
    int y = 31 + (int)(tri(now, 1150) * 19);
    for (int yy = 30; yy < 53; yy += 4) d.drawFastVLine(64 + ox, yy, 2, SH110X_WHITE);
    int lp = constrain(y - 4, 30, 43), rp = constrain(y - 3, 30, 43);
    d.fillRect(20 + ox, lp, 2, 10, SH110X_WHITE);
    d.fillRect(106 + ox, rp, 2, 10, SH110X_WHITE);
    d.fillRect(x + ox, y, 2, 2, SH110X_WHITE);
}
