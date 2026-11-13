#include "Asteroids.h"

#include <Adafruit_SH110X.h>

static const float TURN = 4.2f;        // rad/s
static const float THRUST = 75.0f;     // px/s²
static const float MAX_V = 75.0f;
static const float SHOT_V = 120.0f;
static const uint32_t SHOT_LIFE = 800;
static const uint32_t SHOT_EVERY = 220;  // A held: auto fire
static const uint32_t SAFE_MS = 2500;
static const int W = 128, H = 64;

static float wrapf(float v, float m) {
    while (v < 0) v += m;
    while (v >= m) v -= m;
    return v;
}

// shortest distance on a screen that wraps around
static float wrapDelta(float d, float m) {
    if (d > m / 2) d -= m;
    if (d < -m / 2) d += m;
    return d;
}

void Asteroids::start(uint32_t now) {
    score = 0;
    lives = 3;
    wave = 1;
    nextLife = 5000;
    newRecord = false;
    prev = {true, true, true, true, true};
    for (Spark& s : sparks) s.until = 0;
    respawn(now);
    newWave(now);
}

void Asteroids::respawn(uint32_t now) {
    sx = 64;
    sy = 32;
    svx = svy = 0;
    sa = 0;
    thrusting = false;
    safeUntil = now + SAFE_MS;
    for (Shot& s : shots) s.until = 0;
}

void Asteroids::addRock(float x, float y, uint8_t size, float vx, float vy) {
    for (Rock& r : rocks) {
        if (r.size) continue;
        r.x = x;
        r.y = y;
        r.vx = vx;
        r.vy = vy;
        r.size = size;
        r.a = random(0, 628) / 100.0f;
        r.va = (random(0, 2) ? 1 : -1) * random(40, 160) / 100.0f;
        for (int i = 0; i < 8; i++) r.rad[i] = random(68, 101);   // % of the radius: a lumpy rock
        return;
    }
}

void Asteroids::newWave(uint32_t now) {
    for (Rock& r : rocks) r.size = 0;
    int n = min(2 + wave, 6);
    float v = 9 + min(wave, 6) * 1.5f;
    for (int i = 0; i < n; i++) {
        // somewhere far from the ship
        float x, y;
        do {
            x = random(0, W);
            y = random(0, H);
        } while (fabsf(wrapDelta(x - sx, W)) < 28 && fabsf(wrapDelta(y - sy, H)) < 20);
        float ang = random(0, 628) / 100.0f;
        float sp = v * random(80, 121) / 100.0f;
        addRock(x, y, 3, cosf(ang) * sp, sinf(ang) * sp);
    }
    safeUntil = max(safeUntil, now + 1500);
    phase = P_WAVE;
    phaseAt = now;
    lastFrame = now;
}

void Asteroids::pause() {
    if (phase != P_PLAY && phase != P_WAVE && phase != P_DEAD) return;
    pausedFrom = phase;
    phase = P_PAUSE;
    pauseSel = 0;
    thrusting = false;
    led.off();
}

void Asteroids::addSparks(float x, float y, int n, float sp, uint32_t now) {
    for (Spark& s : sparks) {
        if (n <= 0) break;
        if (s.until > now) continue;
        float ang = random(0, 628) / 100.0f;
        float v = sp * random(40, 101) / 100.0f;
        s.x = x;
        s.y = y;
        s.vx = cosf(ang) * v;
        s.vy = sinf(ang) * v;
        s.until = now + random(250, 500);
        n--;
    }
}

void Asteroids::hitRock(int i, uint32_t now) {
    Rock r = rocks[i];
    rocks[i].size = 0;
    score += r.size == 3 ? 20 : r.size == 2 ? 50 : 100;
    if (score >= nextLife) {
        nextLife += 5000;
        if (lives < 5) lives++;
        led.set(0, 40, 10, 300, now);
    } else {
        led.set(30, 12, 0, 40, now);
    }
    addSparks(r.x, r.y, r.size + 2, 40, now);
    if (r.size > 1) {
        // two smaller ones, a bit faster, going apart
        float sp = sqrtf(r.vx * r.vx + r.vy * r.vy) * 1.35f + 6;
        float base = atan2f(r.vy, r.vx);
        for (int k = -1; k <= 1; k += 2) {
            float ang = base + k * (random(35, 80) / 100.0f);
            addRock(r.x, r.y, r.size - 1, cosf(ang) * sp, sinf(ang) * sp);
        }
    }
}

void Asteroids::shipHit(uint32_t now) {
    addSparks(sx, sy, 10, 35, now);
    led.set(45, 0, 0, 500, now);
    lives--;
    thrusting = false;
    phase = P_DEAD;
    phaseAt = now;
}

void Asteroids::gameOver(uint32_t now) {
    newRecord = score > best;
    if (newRecord) {
        best = score;
        GameUi::saveRecord("ast_hi", best);
    }
    phase = P_OVER;
    phaseAt = now;
}

void Asteroids::frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) {
    GameKeys e = GameUi::edges(k, prev);
    led.tick(now);
    uint32_t gap = now - lastFrame;
    float dt = gap > 200 ? 0.0f : min(gap, (uint32_t)40) / 1000.0f;
    lastFrame = now;

    if (phase == P_PAUSE) {
        int r = GameUi::pauseInput(e, pauseSel);
        if (r == 1) {
            phase = pausedFrom;
            lastFrame = now;
        } else if (r == 2) {
            if (score > best) {
                best = score;
                GameUi::saveRecord("ast_hi", best);
            }
            led.off();
            phase = P_OFF;
            return;
        }
        draw(d, now);
        return;
    }
    if (phase == P_OVER) {
        int r = GameUi::overInput(e, now, phaseAt);
        if (r == 1) start(now);
        else if (r == 2) {
            led.off();
            phase = P_OFF;
            return;
        }
        draw(d, now);
        return;
    }
    if (e.down) {
        pause();
        draw(d, now);
        return;
    }

    // --- the ship ---
    bool shipOn = (phase != P_DEAD);
    if (shipOn) {
        if (k.left) sa -= TURN * dt;
        if (k.right) sa += TURN * dt;
        thrusting = k.up;
        if (thrusting) {
            svx += sinf(sa) * THRUST * dt;
            svy -= cosf(sa) * THRUST * dt;
        }
        float drag = 1.0f - 0.45f * dt;
        svx *= drag;
        svy *= drag;
        float v = sqrtf(svx * svx + svy * svy);
        if (v > MAX_V) {
            svx *= MAX_V / v;
            svy *= MAX_V / v;
        }
        sx = wrapf(sx + svx * dt, W);
        sy = wrapf(sy + svy * dt, H);

        if (k.a && (e.a || now - lastShot >= SHOT_EVERY)) {
            for (Shot& s : shots) {
                if (s.until > now) continue;
                s.x = sx + sinf(sa) * 4;
                s.y = sy - cosf(sa) * 4;
                s.vx = sinf(sa) * SHOT_V + svx * 0.5f;
                s.vy = -cosf(sa) * SHOT_V + svy * 0.5f;
                s.until = now + SHOT_LIFE;
                lastShot = now;
                break;
            }
        }
    }

    // --- shots, rocks, sparks ---
    for (Shot& s : shots) {
        if (s.until <= now) continue;
        s.x = wrapf(s.x + s.vx * dt, W);
        s.y = wrapf(s.y + s.vy * dt, H);
    }
    for (Rock& r : rocks) {
        if (!r.size) continue;
        r.x = wrapf(r.x + r.vx * dt, W);
        r.y = wrapf(r.y + r.vy * dt, H);
        r.a += r.va * dt;
    }
    for (Spark& s : sparks) {
        if (s.until <= now) continue;
        s.x += s.vx * dt;
        s.y += s.vy * dt;
    }

    // shots against rocks
    for (Shot& s : shots) {
        if (s.until <= now) continue;
        for (int i = 0; i < MAX_ROCKS; i++) {
            Rock& r = rocks[i];
            if (!r.size) continue;
            float dx = wrapDelta(s.x - r.x, W), dy = wrapDelta(s.y - r.y, H);
            float rr = radius(r.size) + 1;
            if (dx * dx + dy * dy <= rr * rr) {
                s.until = 0;
                hitRock(i, now);
                break;
            }
        }
    }

    // rocks against the ship
    if (shipOn && now >= safeUntil && phase == P_PLAY) {
        for (Rock& r : rocks) {
            if (!r.size) continue;
            float dx = wrapDelta(sx - r.x, W), dy = wrapDelta(sy - r.y, H);
            float rr = radius(r.size) * 0.85f + 2.5f;
            if (dx * dx + dy * dy <= rr * rr) {
                shipHit(now);
                break;
            }
        }
    }

    // phase changes
    bool anyRock = false;
    for (const Rock& r : rocks) anyRock |= (r.size != 0);
    if (phase == P_WAVE && now - phaseAt >= 1300) phase = P_PLAY;
    if (phase == P_PLAY && !anyRock) {
        wave++;
        newWave(now);
    }
    if (phase == P_DEAD && now - phaseAt >= 1600) {
        if (lives <= 0) gameOver(now);
        else {
            respawn(now);
            phase = anyRock ? P_PLAY : P_WAVE;
            phaseAt = now;
            if (!anyRock) {
                wave++;
                newWave(now);
            }
        }
    }
    draw(d, now);
}

void Asteroids::drawRock(Adafruit_GFX& d, const Rock& r, float x, float y) {
    float rad = radius(r.size);
    int px[8], py[8];
    for (int i = 0; i < 8; i++) {
        float ang = r.a + i * 0.785398f;
        float rr = rad * r.rad[i] / 100.0f;
        px[i] = (int)lroundf(x + cosf(ang) * rr);
        py[i] = (int)lroundf(y + sinf(ang) * rr);
    }
    for (int i = 0; i < 8; i++) d.drawLine(px[i], py[i], px[(i + 1) % 8], py[(i + 1) % 8], SH110X_WHITE);
}

void Asteroids::drawShip(Adafruit_GFX& d, float x, float y, float a, bool flame) {
    float s = sinf(a), c = cosf(a);
    // nose 5 px ahead, two back corners
    auto P = [&](float fx, float fy, int& ox, int& oy) {
        // fx: right, fy: forward
        ox = (int)lroundf(x + fx * c + fy * s);
        oy = (int)lroundf(y + fx * s - fy * c);
    };
    int nx, ny, lx, ly, rx, ry, bx, by;
    P(0, 5, nx, ny);
    P(-3.5f, -3.5f, lx, ly);
    P(3.5f, -3.5f, rx, ry);
    P(0, -1.5f, bx, by);
    d.drawLine(nx, ny, lx, ly, SH110X_WHITE);
    d.drawLine(nx, ny, rx, ry, SH110X_WHITE);
    d.drawLine(lx, ly, bx, by, SH110X_WHITE);
    d.drawLine(rx, ry, bx, by, SH110X_WHITE);
    if (flame) {
        int fx, fy;
        P(0, -6, fx, fy);
        d.drawLine(bx, by, fx, fy, SH110X_WHITE);
    }
}

void Asteroids::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    // rocks (also on the other side when they cross an edge)
    for (const Rock& r : rocks) {
        if (!r.size) continue;
        float rad = radius(r.size) + 1;
        for (int ox = -1; ox <= 1; ox++) {
            for (int oy = -1; oy <= 1; oy++) {
                float x = r.x + ox * W, y = r.y + oy * H;
                if (x + rad < 0 || x - rad >= W || y + rad < 0 || y - rad >= H) continue;
                drawRock(d, r, x, y);
            }
        }
    }
    for (const Shot& s : shots) {
        if (s.until > now) d.drawPixel((int)s.x, (int)s.y, SH110X_WHITE);
    }
    for (const Spark& s : sparks) {
        if (s.until > now) d.drawPixel((int)s.x, (int)s.y, SH110X_WHITE);
    }

    bool shipVisible = phase != P_DEAD && phase != P_OVER;
    if (shipVisible && now < safeUntil && (now / 120) % 2) shipVisible = false;   // blinking while safe
    if (shipVisible) drawShip(d, sx, sy, sa, thrusting && (now / 45) % 2);

    // score (small digits) and lives
    GameUi::tinyNumber(d, 1, 1, String(score), SH110X_WHITE);
    for (int i = 0; i < lives && i < 5; i++) {
        int x = 124 - i * 6;
        d.drawLine(x, 1, x - 2, 6, SH110X_WHITE);
        d.drawLine(x, 1, x + 2, 6, SH110X_WHITE);
        d.drawFastHLine(x - 1, 5, 3, SH110X_WHITE);
    }

    if (phase == P_WAVE && (now - phaseAt) < 1300) {
        String t = "OLEADA " + String(wave);
        int w = t.length() * 6 + 8;
        d.fillRect(64 - w / 2, 44, w, 11, SH110X_BLACK);
        GameUi::centerText(d, t, 46);
    }
    if (phase == P_PAUSE) GameUi::drawPause(d, pauseSel);
    if (phase == P_OVER) GameUi::drawOver(d, "FIN DEL JUEGO", String(score), "Record " + String(best), newRecord, now, phaseAt);
}

void Asteroids::drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {
    // the ship turning in the middle, shooting at two rocks that tumble by
    float a = now / 700.0f;
    drawShip(d, 64 + ox, 41, a, (now / 90) % 3 == 0);
    Rock r1 = {0, 0, 0, 0, now / 900.0f, 0, 3, {95, 80, 100, 72, 90, 100, 78, 88}};
    Rock r2 = {0, 0, 0, 0, -(now / 600.0f), 0, 2, {100, 75, 92, 85, 70, 96, 88, 80}};
    drawRock(d, r1, 24 + ox, 40);
    drawRock(d, r2, 104 + ox, 44);
    // a shot every 600 ms, flying away from the nose
    uint32_t p = now % 600;
    float t = p / 600.0f;
    float shotA = a - (p / 700.0f);
    float dist = 6 + t * 22;
    int x = 64 + ox + (int)(sinf(shotA) * dist), y = 41 - (int)(cosf(shotA) * dist);
    if (y >= 29 && y <= 53) d.drawPixel(x, y, SH110X_WHITE);
}
