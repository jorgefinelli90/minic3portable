#include "Arkanoid.h"

#include <Adafruit_SH110X.h>
#include <Preferences.h>

#include "MiniC3.h"

// ---------------------------------------------------------------------------
//  Field (128 x 64)
//    y 0-7    score · level · lives
//    y 9      top wall (x 0 and x 127 are the side walls)
//    y 12-36  bricks: 10 x 5, 11x4 px each with a 1 px gap
//    y 58-60  paddle
// ---------------------------------------------------------------------------

static const int WALL_Y = 9;
static const int FIELD_TOP = 10;
static const int BX0 = 4;
static const int BY0 = 12;
static const int CELL_W = 12;
static const int CELL_H = 5;
static const int BRICK_W = 11;
static const int BRICK_H = 4;
static const int PADDLE_Y = 58;
static const int PADDLE_W = 18;
static const int PADDLE_WIDE = 28;
static const int BALL = 2;
static const int MAX_LIVES = 5;

static const uint32_t INTRO_MS = 1300;
static const uint32_t DEAD_MS = 1300;
static const uint32_t CLEAR_MS = 2000;
static const uint32_t WIDE_MS = 15000;

// '0' empty, '1' one hit, '2' two hits
static const char* const LEVELS[6][Arkanoid::ROWS] = {
    {"2222222222", "1111111111", "1111111111", "1111111111", "0000000000"},
    {"0000220000", "0001111000", "0011111100", "0111111110", "1111111111"},
    {"1010101010", "0202020202", "1010101010", "0202020202", "1010101010"},
    {"0220000220", "1221001221", "1111111111", "0111111110", "0001111000"},
    {"0100000010", "0022222200", "0220220220", "1111111111", "1011111101"},
    {"2222222222", "2111111112", "2101111012", "2111111112", "2222002222"},
};
static const char* const LEVEL_NAMES[6] = {"Clasico", "Piramide", "Damero", "Corazon", "Invasor", "Fortaleza"};

// 5x3 icons inside the capsules (one row per byte, bit 4 = left pixel)
static const uint8_t CAP_ICONS[4][3] = {
    {0b10001, 0b11111, 0b10001},   // wide paddle  |=|
    {0b11111, 0b01110, 0b00100},   // slow ball    v
    {0b00000, 0b10101, 0b00000},   // 3 balls      ...
    {0b11011, 0b11111, 0b01110},   // extra life   heart
};

// ---------------------------------------------------------------------------

void Arkanoid::begin() {
    Preferences p;
    p.begin("minic3_game", true);
    best = p.getUInt("ark_hi", 0);
    bestLevel = p.getUChar("ark_lv", 0);
    p.end();
}

void Arkanoid::saveRecord() {
    Preferences p;
    p.begin("minic3_game", false);
    p.putUInt("ark_hi", best);
    p.putUChar("ark_lv", bestLevel);
    p.end();
}

float Arkanoid::baseSpeed() const {
    return min(50.0f + (level - 1) * 4.0f, 82.0f);
}

float Arkanoid::maxSpeed() const {
    return min(baseSpeed() * 1.7f, 118.0f);
}

void Arkanoid::setPhase(Phase p, uint32_t now) {
    phase = p;
    phaseAt = now;
}

void Arkanoid::start(uint32_t now) {
    score = 0;
    shownScore = 0;
    lives = 3;
    level = 1;
    newRecord = false;
    pauseSel = 0;
    touchWas = true;
    for (Part& p : parts) p.alive = false;
    loadLevel();
    resetPaddleAndBall();
    lastFrame = now;
    setPhase(P_INTRO, now);
}

void Arkanoid::loadLevel() {
    const char* const* rows = LEVELS[(level - 1) % 6];
    bricksLeft = 0;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            bricks[r][c] = (uint8_t)(rows[r][c] - '0');
            if (bricks[r][c]) bricksLeft++;
        }
    }
    hitR = hitC = -1;
}

void Arkanoid::resetPaddleAndBall() {
    pw = PADDLE_W;
    pwTarget = PADDLE_W;
    wideUntil = 0;
    px = 64 - PADDLE_W / 2;
    holdTime = 0;
    speed = baseSpeed();
    for (Ball& b : balls) b.alive = false;
    for (Cap& c : caps) c.alive = false;
    balls[0].alive = true;
    balls[0].x = px + pw / 2 - 1;
    balls[0].y = PADDLE_Y - BALL;
    balls[0].dx = 0;
    balls[0].dy = -1;
}

void Arkanoid::pause() {
    if (phase != P_PLAY && phase != P_READY && phase != P_INTRO) return;
    pausedFrom = (phase == P_PLAY) ? P_PLAY : P_READY;
    if (phase == P_INTRO) hitR = hitC = -1;
    pauseSel = 0;
    ledOff();
    setPhase(P_PAUSE, millis());
}

void Arkanoid::onEvent(InputEvent e) {
    switch (phase) {
        case P_PAUSE:
            if (e == INPUT_UP || e == INPUT_DOWN) pauseSel ^= 1;
            break;
        case P_PLAY:
        case P_READY:
            if (e == INPUT_DOWN) pause();
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
//  LED (the C3-Zero LED takes RGB, rgbLedWrite sends GRB: swap R and G)
// ---------------------------------------------------------------------------

void Arkanoid::led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms, uint32_t now) {
#if MINI_LED_RGB_ORDER
    rgbLedWrite(MINI_RGB_LED_PIN, g, r, b);
#else
    rgbLedWrite(MINI_RGB_LED_PIN, r, g, b);
#endif
    ledOn = true;
    ledUntil = now + ms;
}

void Arkanoid::ledOff() {
    if (!ledOn) return;
    rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);
    ledOn = false;
}

// ---------------------------------------------------------------------------
//  Update
// ---------------------------------------------------------------------------

void Arkanoid::frame(Adafruit_GFX& d, bool left, bool right, bool touch, uint32_t now) {
    uint32_t gap = now - lastFrame;
    float dt = (gap > 200) ? 0.0f : min(gap, (uint32_t)40) / 1000.0f;   // after a pause: no jump
    lastFrame = now;

    bool pressed = touch && !touchWas;
    touchWas = touch;

    update(dt, left, right, pressed, now);
    if (phase != P_OFF) draw(d, now);
}

void Arkanoid::update(float dt, bool left, bool right, bool pressed, uint32_t now) {
    uint32_t inPhase = now - phaseAt;

    if (ledOn && (int32_t)(now - ledUntil) >= 0) ledOff();

    // particles fly in every phase
    for (Part& p : parts) {
        if (!p.alive) continue;
        p.vy += 170.0f * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        uint16_t ms = (uint16_t)(dt * 1000);
        if (p.life <= ms || p.y > 64 || p.x < 0 || p.x > 127) p.alive = false;
        else p.life -= ms;
    }

    // the HUD score counts up
    if (shownScore < score) shownScore += max((uint32_t)1, (score - shownScore) / 5);

    switch (phase) {
        case P_PAUSE:
            if (pressed) {
                if (pauseSel == 0) {
                    phase = pausedFrom;   // phaseAt is not used by READY / PLAY
                    if (wideUntil) wideUntil += inPhase;
                } else {
                    if (score > best || level > bestLevel) {
                        best = max(best, score);
                        bestLevel = max(bestLevel, (uint8_t)level);
                        saveRecord();
                    }
                    ledOff();
                    phase = P_OFF;
                }
            }
            return;

        case P_INTRO:
            if (inPhase >= INTRO_MS) setPhase(P_READY, now);
            return;

        case P_DEAD:
            if (inPhase >= DEAD_MS) {
                if (lives > 0) {
                    resetPaddleAndBall();
                    setPhase(P_READY, now);
                } else {
                    finishGame();
                }
            }
            return;

        case P_CLEAR:
            if (inPhase >= CLEAR_MS) {
                level++;
                loadLevel();
                resetPaddleAndBall();
                setPhase(P_INTRO, now);
            } else if (inPhase > 300 && inPhase < 1500 && (inPhase / 250) % 2 == 0 && !ledOn) {
                led(30, 26, 0, 120, now);   // yellow pulses
            }
            return;

        case P_OVER:
            if (pressed && inPhase > 900) {
                ledOff();
                phase = P_OFF;
            }
            return;

        default:
            break;
    }

    // --- READY / PLAY: paddle ---
    if (wideUntil && (int32_t)(now - wideUntil) >= 0) {
        wideUntil = 0;
        pwTarget = PADDLE_W;
    }
    if ((int)(pw + 0.5f) != pwTarget) {
        float center = px + pw / 2;
        float step = 45.0f * dt;
        if (pw < pwTarget) pw = min(pw + step, (float)pwTarget);
        else pw = max(pw - step, (float)pwTarget);
        px = center - pw / 2;
    }

    int dir = (right ? 1 : 0) - (left ? 1 : 0);
    if (dir != 0) {
        holdTime += dt;
        float v = 80.0f + 85.0f * min(holdTime / 0.35f, 1.0f);   // speeds up while held
        px += dir * v * dt;
    } else {
        holdTime = 0;
    }
    px = constrain(px, 1.0f, 127.0f - pw);

    if (phase == P_READY) {
        Ball& b = balls[0];
        b.x = px + pw / 2 - 1;
        b.y = PADDLE_Y - BALL;
        if (pressed) {
            // launch: up, a bit to the side the paddle is moving (or random)
            float ang = (dir != 0) ? dir * 0.35f : (random(-30, 31) / 100.0f);
            if (fabsf(ang) < 0.12f) ang = (ang < 0 ? -0.18f : 0.18f);
            b.dx = sinf(ang);
            b.dy = -cosf(ang);
            setPhase(P_PLAY, now);
        }
        return;
    }

    // --- PLAY ---
    if (pressed) {
        pause();
        return;
    }

    int alive = 0;
    for (Ball& b : balls) {
        if (!b.alive) continue;
        updateBall(b, dt, now);
        if (phase != P_PLAY) return;   // the last brick was broken
        if (b.alive) alive++;
    }

    // capsules
    for (Cap& c : caps) {
        if (!c.alive) continue;
        c.y += 26.0f * dt;
        if (c.y + 7 >= PADDLE_Y && c.y <= PADDLE_Y + 3 && c.x + 11 > px && c.x < px + pw) {
            c.alive = false;
            applyCap(c.type, now);
        } else if (c.y > 64) {
            c.alive = false;
        }
    }

    if (alive == 0) loseLife(now);
}

bool Arkanoid::brickAt(float x, float y, int& r, int& c) {
    if (y + BALL <= BY0 || y >= BY0 + ROWS * CELL_H) return false;
    int c0 = (int)floorf((x - BX0) / CELL_W);
    int c1 = (int)floorf((x + BALL - 0.01f - BX0) / CELL_W);
    int r0 = (int)floorf((y - BY0) / CELL_H);
    int r1 = (int)floorf((y + BALL - 0.01f - BY0) / CELL_H);
    for (int rr = r0; rr <= r1; rr++) {
        if (rr < 0 || rr >= ROWS) continue;
        for (int cc = c0; cc <= c1; cc++) {
            if (cc < 0 || cc >= COLS || !bricks[rr][cc]) continue;
            float bx = BX0 + cc * CELL_W;
            float by = BY0 + rr * CELL_H;
            if (x < bx + BRICK_W && x + BALL > bx && y < by + BRICK_H && y + BALL > by) {
                r = rr;
                c = cc;
                return true;
            }
        }
    }
    return false;
}

void Arkanoid::updateBall(Ball& b, float dt, uint32_t now) {
    float dist = speed * dt;
    int steps = (int)ceilf(dist / 0.8f);
    if (steps < 1) steps = 1;
    float stepLen = dist / steps;
    int r, c;

    for (int i = 0; i < steps; i++) {
        float sx = b.dx * stepLen;
        float sy = b.dy * stepLen;

        // --- horizontal ---
        b.x += sx;
        if (b.x < 1) {
            b.x = 1;
            b.dx = fabsf(b.dx);
        } else if (b.x > 127 - BALL) {
            b.x = 127 - BALL;
            b.dx = -fabsf(b.dx);
        }
        if (brickAt(b.x, b.y, r, c)) {
            b.x -= sx;
            b.dx = -b.dx;
            hitBrick(r, c, now);
            if (phase != P_PLAY) return;
        }

        // --- vertical ---
        b.y += sy;
        if (b.y < FIELD_TOP) {
            b.y = FIELD_TOP;
            b.dy = fabsf(b.dy);
        }
        if (brickAt(b.x, b.y, r, c)) {
            b.y -= sy;
            b.dy = -b.dy;
            hitBrick(r, c, now);
            if (phase != P_PLAY) return;
        }

        // --- paddle: the angle depends on where it hits ---
        float bottom = b.y + BALL;
        if (b.dy > 0 && bottom >= PADDLE_Y && bottom - sy < PADDLE_Y + 1.5f && b.x + BALL > px && b.x < px + pw) {
            float rel = ((b.x + 1) - (px + pw / 2)) / (pw / 2 + 1);
            rel = constrain(rel, -1.0f, 1.0f);
            float ang = rel * 1.05f;   // up to ~60 degrees
            // the same path over and over (hunting the last bricks): bend it a little
            if (++dryBounces >= 5) ang = constrain(ang + random(-25, 26) / 100.0f, -1.1f, 1.1f);
            b.dx = sinf(ang);
            b.dy = -cosf(ang);
            b.y = PADDLE_Y - BALL;
            speed = min(speed * 1.02f, maxSpeed());
        }

        if (b.y > 64) {
            b.alive = false;
            return;
        }
    }

    // never too flat (it would bounce side to side forever), always unit length
    if (fabsf(b.dy) < 0.3f) b.dy = (b.dy < 0 ? -0.3f : 0.3f);
    float dyy = b.dy * b.dy;
    b.dx = (b.dx < 0 ? -1.0f : 1.0f) * sqrtf(1.0f - dyy);
}

void Arkanoid::hitBrick(int r, int c, uint32_t now) {
    uint8_t& br = bricks[r][c];
    if (!br) return;
    br--;
    dryBounces = 0;
    uint32_t mult = 1 + (level - 1) / 6;   // faster laps are worth more
    score += 10 * mult;
    float cx = BX0 + c * CELL_W + BRICK_W / 2.0f;
    float cy = BY0 + r * CELL_H + BRICK_H / 2.0f;

    if (br == 0) {
        bricksLeft--;
        burst(cx, cy, 6, 1.0f);
        led(14, 14, 14, 35, now);
        if (random(100) < 16) dropCap(cx - 5, cy - 3);
        if (bricksLeft <= 0) {
            clearBonus = 100 * level + 50 * lives;
            score += clearBonus;
            for (Cap& k : caps) k.alive = false;
            led(30, 26, 0, 200, now);
            setPhase(P_CLEAR, now);
        }
    } else {
        hitR = r;
        hitC = c;
        hitAt = now;
        burst(cx, cy, 2, 0.5f);
        led(0, 0, 22, 35, now);
    }
}

void Arkanoid::dropCap(float x, float y) {
    for (Cap& c : caps) {
        if (c.alive) continue;
        int roll = random(100);
        uint8_t t = (roll < 35) ? CAP_WIDE : (roll < 58) ? CAP_SLOW : (roll < 88) ? CAP_MULTI : CAP_LIFE;
        if (t == CAP_LIFE && lives >= MAX_LIVES) t = CAP_WIDE;
        c.type = t;
        c.x = constrain(x, 1.0f, 116.0f);
        c.y = y;
        c.alive = true;
        return;
    }
}

void Arkanoid::applyCap(uint8_t type, uint32_t now) {
    score += 50;
    switch (type) {
        case CAP_WIDE:
            pwTarget = PADDLE_WIDE;
            wideUntil = now + WIDE_MS;
            led(0, 0, 40, 180, now);
            break;
        case CAP_SLOW:
            speed = baseSpeed() * 0.8f;
            led(0, 30, 30, 180, now);
            break;
        case CAP_MULTI: {
            Ball* src = nullptr;
            for (Ball& b : balls) {
                if (b.alive) { src = &b; break; }
            }
            if (!src) break;
            // the new balls leave upward, one to each side of the original
            float base = atan2f(src->dx, fabsf(src->dy));
            int made = 0;
            for (Ball& b : balls) {
                if (b.alive) continue;
                float ang = constrain(base + (made == 0 ? 0.5f : -0.5f), -1.2f, 1.2f);
                b = *src;
                b.dx = sinf(ang);
                b.dy = -cosf(ang);
                b.alive = true;
                if (++made == 2) break;
            }
            led(30, 0, 30, 180, now);
            break;
        }
        case CAP_LIFE:
            if (lives < MAX_LIVES) lives++;
            led(0, 40, 0, 250, now);
            break;
    }
}

void Arkanoid::burst(float x, float y, int n, float spread) {
    for (Part& p : parts) {
        if (n <= 0) break;
        if (p.alive) continue;
        p.x = x + random(-3, 4) * spread;
        p.y = y;
        p.vx = random(-45, 46) * spread;
        p.vy = random(-70, 5) * spread;
        p.life = 300 + random(0, 350);
        p.alive = true;
        n--;
    }
}

void Arkanoid::loseLife(uint32_t now) {
    lives--;
    burst(px + pw / 2, PADDLE_Y + 1, 16, 1.6f);
    shakeUntil = now + 380;
    for (Cap& c : caps) c.alive = false;
    led(45, 0, 0, 350, now);
    setPhase(P_DEAD, now);
}

void Arkanoid::finishGame() {
    newRecord = (score > best);
    bool betterLevel = (level > bestLevel);
    if (newRecord) best = score;
    if (betterLevel) bestLevel = (uint8_t)level;
    if (newRecord || betterLevel) saveRecord();
    if (newRecord) led(30, 26, 0, 600, millis());
    setPhase(P_OVER, millis());
}

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------

void Arkanoid::centerText(Adafruit_GFX& d, const String& s, int y, int size) {
    d.setTextSize(size);
    int w = s.length() * 6 * size - size;
    d.setCursor((128 - w) / 2, y);
    d.print(s);
    d.setTextSize(1);
}

void Arkanoid::drawHud(Adafruit_GFX& d) {
    d.setFont();
    d.setTextSize(1);
    d.setTextColor(SH110X_WHITE);
    char buf[8];
    snprintf(buf, sizeof(buf), "%06lu", (unsigned long)min(shownScore, (uint32_t)999999));
    d.setCursor(0, 0);
    d.print(buf);

    String lv = "NIVEL " + String(level);
    d.setCursor(44 + (40 - (int)lv.length() * 6) / 2, 0);
    d.print(lv);

    // lives = small paddles, right aligned
    for (int i = 0; i < lives && i < MAX_LIVES; i++) {
        int x = 122 - i * 7;
        d.fillRect(x + 1, 2, 4, 3, SH110X_WHITE);
        d.drawPixel(x, 3, SH110X_WHITE);
        d.drawPixel(x + 5, 3, SH110X_WHITE);
    }
}

void Arkanoid::drawPaddle(Adafruit_GFX& d, int x, int w) {
    // the Vaus: rounded ends with a notch near each end
    d.fillRect(x + 1, PADDLE_Y + oy, w - 2, 3, SH110X_WHITE);
    d.drawFastVLine(x, PADDLE_Y + 1 + oy, 1, SH110X_WHITE);
    d.drawFastVLine(x + w - 1, PADDLE_Y + 1 + oy, 1, SH110X_WHITE);
    d.drawPixel(x + 3, PADDLE_Y + 1 + oy, SH110X_BLACK);
    d.drawPixel(x + w - 4, PADDLE_Y + 1 + oy, SH110X_BLACK);
}

void Arkanoid::drawBricks(Adafruit_GFX& d, uint32_t now) {
    uint32_t inPhase = now - phaseAt;
    for (int r = 0; r < ROWS; r++) {
        int y0 = BY0 + r * CELL_H;
        if (phase == P_INTRO) {
            // the rows fall in one after another
            long t = (long)inPhase - (ROWS - 1 - r) * 90;
            if (t < 0) continue;
            if (t < 260) {
                float k = t / 260.0f;
                float e = 1.0f - (1.0f - k) * (1.0f - k);
                y0 = (int)(-6 + (y0 + 6) * e);
            }
        }
        for (int c = 0; c < COLS; c++) {
            uint8_t hp = bricks[r][c];
            if (!hp) continue;
            int x = BX0 + c * CELL_W + ox;
            int y = y0 + oy;
            bool flash = (r == hitR && c == hitC && now - hitAt < 90);
            if (flash) {
                d.drawRect(x, y, BRICK_W, BRICK_H, SH110X_WHITE);
            } else if (hp == 1) {
                d.fillRect(x, y, BRICK_W, BRICK_H, SH110X_WHITE);
            } else {
                // two hits: outline with a dotted middle ("metal")
                d.drawRect(x, y, BRICK_W, BRICK_H, SH110X_WHITE);
                for (int k = 1; k < BRICK_W - 1; k++) d.drawPixel(x + k, y + 1 + (k & 1), SH110X_WHITE);
            }
        }
    }
}

void Arkanoid::drawCap(Adafruit_GFX& d, const Cap& c, uint32_t now) {
    int x = (int)c.x + ox;
    int y = (int)c.y + oy;
    bool inv = (now / 180) % 2;
    if (inv) d.fillRoundRect(x, y, 11, 7, 2, SH110X_WHITE);
    else d.drawRoundRect(x, y, 11, 7, 2, SH110X_WHITE);
    uint16_t col = inv ? SH110X_BLACK : SH110X_WHITE;
    for (int row = 0; row < 3; row++) {
        for (int bit = 0; bit < 5; bit++) {
            if (CAP_ICONS[c.type][row] & (0x10 >> bit)) d.drawPixel(x + 3 + bit, y + 2 + row, col);
        }
    }
}

void Arkanoid::drawBanner(Adafruit_GFX& d, const char* l1, const String& l2, bool invert) {
    d.fillRect(12, 38, 104, 19, invert ? SH110X_WHITE : SH110X_BLACK);
    d.drawRect(12, 38, 104, 19, SH110X_WHITE);
    d.setTextColor(invert ? SH110X_BLACK : SH110X_WHITE);
    centerText(d, l1, 40);
    if (l2.length()) centerText(d, l2, 48);
    d.setTextColor(SH110X_WHITE);
}

void Arkanoid::drawField(Adafruit_GFX& d, uint32_t now) {
    d.drawFastHLine(0 + ox, WALL_Y + oy, 128, SH110X_WHITE);
    d.drawFastVLine(0 + ox, WALL_Y + oy, 64 - WALL_Y, SH110X_WHITE);
    d.drawFastVLine(127 + ox, WALL_Y + oy, 64 - WALL_Y, SH110X_WHITE);

    drawBricks(d, now);

    for (const Cap& c : caps) {
        if (c.alive) drawCap(d, c, now);
    }

    if (phase != P_DEAD) drawPaddle(d, (int)(px + 0.5f) + ox, (int)(pw + 0.5f));

    if (phase == P_READY || phase == P_PLAY || phase == P_PAUSE || phase == P_CLEAR) {
        for (const Ball& b : balls) {
            if (b.alive) d.fillRect((int)(b.x + 0.5f) + ox, (int)(b.y + 0.5f) + oy, BALL, BALL, SH110X_WHITE);
        }
    }

    for (const Part& p : parts) {
        if (p.alive) d.drawPixel((int)p.x + ox, (int)p.y + oy, SH110X_WHITE);
    }
}

void Arkanoid::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    if ((int32_t)(shakeUntil - now) > 0) {
        ox = (int)random(-2, 3);
        oy = (int)random(-1, 2);
    } else {
        ox = oy = 0;
    }

    drawHud(d);
    drawField(d, now);
    ox = oy = 0;

    uint32_t inPhase = now - phaseAt;
    String lvName = LEVEL_NAMES[(level - 1) % 6];
    if (level > 6) lvName += " +" + String((level - 1) / 6);

    switch (phase) {
        case P_INTRO:
            if (inPhase > 600) {
                centerText(d, "NIVEL " + String(level), 38);
                centerText(d, lvName, 47);
            }
            break;

        case P_READY:
            centerText(d, "NIVEL " + String(level), 38);
            if ((now / 450) % 2 == 0) centerText(d, "A: lanzar", 47);
            break;

        case P_PAUSE: {
            d.fillRect(24, 14, 80, 44, SH110X_BLACK);
            d.drawRect(24, 14, 80, 44, SH110X_WHITE);
            d.fillRect(24, 14, 80, 11, SH110X_WHITE);
            d.setTextColor(SH110X_BLACK);
            centerText(d, "PAUSA", 16);
            d.setTextColor(SH110X_WHITE);
            const char* opts[2] = {"Seguir", "Salir"};
            for (int i = 0; i < 2; i++) {
                int y = 29 + i * 12;
                if (i == pauseSel) {
                    d.fillRect(30, y - 2, 68, 11, SH110X_WHITE);
                    d.setTextColor(SH110X_BLACK);
                }
                d.setCursor(36, y);
                d.print(i == pauseSel ? ">" : " ");
                d.print(" ");
                d.print(opts[i]);
                d.setTextColor(SH110X_WHITE);
            }
            break;
        }

        case P_CLEAR: {
            uint32_t shown = (inPhase < 300) ? 0 : min(clearBonus, (uint32_t)((inPhase - 300) * clearBonus / 700));
            drawBanner(d, "NIVEL SUPERADO!", "+" + String(shown), (inPhase / 200) % 2 == 0 && inPhase < 800);
            break;
        }

        case P_DEAD:
            if (lives > 0 && inPhase > 350) {
                drawBanner(d, lives == 1 ? "ULTIMA VIDA" : "TE QUEDAN", lives == 1 ? String("") : String(lives) + " vidas", false);
            } else if (lives == 0 && inPhase > 350) {
                drawBanner(d, "SIN VIDAS", "", false);
            }
            break;

        case P_OVER: {
            d.fillRoundRect(6, 11, 116, 52, 4, SH110X_BLACK);
            d.drawRoundRect(6, 11, 116, 52, 4, SH110X_WHITE);
            centerText(d, "FIN DEL JUEGO", 14);
            // the final score lands with a little bounce
            int y = 25;
            if (inPhase < 400) y = 25 - (int)(10 * (1.0f - inPhase / 400.0f));
            centerText(d, String(score), y, 2);
            if (newRecord) {
                if ((now / 300) % 2 == 0) centerText(d, "NUEVO RECORD!", 43);
            } else {
                centerText(d, "Record " + String(best), 43);
            }
            if (inPhase > 900) centerText(d, "A: volver", 53);
            break;
        }

        default:
            break;
    }
}
