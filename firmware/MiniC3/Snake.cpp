#include "Snake.h"

#include <Adafruit_SH110X.h>

static const int X0 = 2, Y0 = 10, CELL = 4;

void Snake::start(uint32_t now) {
    len = 4;
    for (int i = 0; i < len; i++) {
        bx[i] = 8 - i;
        by[i] = ROWS / 2;
    }
    dx = 1;
    dy = 0;
    queued = 0;
    eaten = 0;
    score = 0;
    newRecord = false;
    bonusX = -1;
    prev = {true, true, true, true, true};
    placeFood();
    phase = P_READY;
    phaseAt = now;
}

void Snake::pause() {
    if (phase != P_PLAY && phase != P_READY) return;
    phase = P_PAUSE;
    pauseSel = 0;
    led.off();
}

bool Snake::occupied(int x, int y, int from) const {
    for (int i = from; i < len; i++) {
        if (bx[i] == x && by[i] == y) return true;
    }
    return false;
}

void Snake::placeFood() {
    for (int tries = 0; tries < 500; tries++) {
        int x = random(0, COLS), y = random(0, ROWS);
        if (!occupied(x, y) && !(x == bonusX && y == bonusY)) {
            foodX = x;
            foodY = y;
            return;
        }
    }
}

void Snake::turn(int8_t ndx, int8_t ndy) {
    // compare with the last direction asked (or the current one)
    int8_t ldx = queued ? queue[queued - 1][0] : dx;
    int8_t ldy = queued ? queue[queued - 1][1] : dy;
    if ((ndx == ldx && ndy == ldy) || (ndx == -ldx && ndy == -ldy)) return;   // no U-turn
    if (queued < 2) {
        queue[queued][0] = ndx;
        queue[queued][1] = ndy;
        queued++;
    }
}

void Snake::die(uint32_t now) {
    phase = P_DEAD;
    phaseAt = now;
    led.set(40, 0, 0, 500, now);
    newRecord = score > best;
    if (newRecord) {
        best = score;
        GameUi::saveRecord("snk_hi", best);
    }
}

void Snake::step(uint32_t now) {
    if (queued) {
        dx = queue[0][0];
        dy = queue[0][1];
        queue[0][0] = queue[1][0];
        queue[0][1] = queue[1][1];
        queued--;
    }
    int nx = bx[0] + dx, ny = by[0] + dy;
    bool ate = (nx == foodX && ny == foodY);
    bool ateBonus = (nx == bonusX && ny == bonusY);
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
        die(now);
        return;
    }
    // its own body; the tail cell is free this step (it moves away) unless it grows
    int check = (ate || ateBonus) ? len : len - 1;
    for (int i = 0; i < check; i++) {
        if (bx[i] == nx && by[i] == ny) {
            die(now);
            return;
        }
    }

    if ((ate || ateBonus) && len < COLS * ROWS) len++;
    for (int i = len - 1; i > 0; i--) {
        bx[i] = bx[i - 1];
        by[i] = by[i - 1];
    }
    bx[0] = nx;
    by[0] = ny;

    if (ate) {
        score += 1;
        eaten++;
        led.set(0, 25, 0, 40, now);
        placeFood();
        if (eaten % 5 == 0 && bonusX < 0) {
            for (int tries = 0; tries < 300; tries++) {
                int x = random(0, COLS), y = random(0, ROWS);
                if (!occupied(x, y) && !(x == foodX && y == foodY)) {
                    bonusX = x;
                    bonusY = y;
                    bonusUntil = now + 5000;
                    break;
                }
            }
        }
    }
    if (ateBonus) {
        uint32_t left = (bonusUntil - now) / 1000;
        score += 5 + left;
        bonusX = -1;
        led.set(30, 26, 0, 150, now);
    }
}

void Snake::frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) {
    GameKeys e = GameUi::edges(k, prev);
    led.tick(now);

    switch (phase) {
        case P_PAUSE: {
            int r = GameUi::pauseInput(e, pauseSel);
            if (r == 1) {
                phase = P_PLAY;
                stepAt = now + 300;
            } else if (r == 2) {
                if (score > best) {
                    best = score;
                    GameUi::saveRecord("snk_hi", best);
                }
                phase = P_OFF;
                return;
            }
            break;
        }
        case P_READY:
            if (e.a) {
                pause();
                break;
            }
            if (e.up || e.down || e.left || e.right || now - phaseAt > 2500) {
                if (e.up) turn(0, -1);
                if (e.down) turn(0, 1);
                if (e.right) turn(1, 0);
                phase = P_PLAY;
                stepAt = now;
            }
            break;
        case P_PLAY: {
            if (e.a) {
                pause();
                break;
            }
            if (e.up) turn(0, -1);
            if (e.down) turn(0, 1);
            if (e.left) turn(-1, 0);
            if (e.right) turn(1, 0);
            if (bonusX >= 0 && (int32_t)(now - bonusUntil) >= 0) bonusX = -1;
            uint32_t interval = max(65, 150 - len * 2);
            if ((int32_t)(now - stepAt) >= 0) {
                stepAt = now + interval;
                step(now);
            }
            break;
        }
        case P_DEAD:
            if (now - phaseAt > 900) {
                phase = P_OVER;
                phaseAt = now;
            }
            break;
        case P_OVER: {
            int r = GameUi::overInput(e, now, phaseAt);
            if (r == 1) start(now);
            else if (r == 2) {
                led.off();
                phase = P_OFF;
                return;
            }
            break;
        }
        default:
            break;
    }
    draw(d, now);
}

void Snake::drawBody(Adafruit_GFX& d, int x0, int y0, const uint8_t* xs, const uint8_t* ys, int n, int8_t hdx, int8_t hdy) {
    for (int i = 0; i < n; i++) {
        int x = x0 + xs[i] * CELL, y = y0 + ys[i] * CELL;
        d.fillRect(x, y, 3, 3, SH110X_WHITE);
        if (i + 1 < n) {   // join with the next piece
            int ddx = xs[i + 1] - xs[i], ddy = ys[i + 1] - ys[i];
            if (ddx == 1) d.drawFastVLine(x + 3, y, 3, SH110X_WHITE);
            if (ddx == -1) d.drawFastVLine(x - 1, y, 3, SH110X_WHITE);
            if (ddy == 1) d.drawFastHLine(x, y + 3, 3, SH110X_WHITE);
            if (ddy == -1) d.drawFastHLine(x, y - 1, 3, SH110X_WHITE);
        }
    }
    // the eye
    if (n > 0) {
        int x = x0 + xs[0] * CELL + 1 + hdx, y = y0 + ys[0] * CELL + 1 + hdy;
        d.drawPixel(x, y, SH110X_BLACK);
    }
}

void Snake::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    d.setCursor(0, 0);
    d.print("PUNTOS " + String(score));
    String r = "REC " + String(max(best, score));
    d.setCursor(128 - (int)r.length() * 6 + 1, 0);
    d.print(r);
    d.drawRect(0, 8, 128, 56, SH110X_WHITE);

    // apple (blinks a little) and the big one with its time running out
    if ((now / 250) % 4 != 0) {
        int fx = X0 + foodX * CELL, fy = Y0 + foodY * CELL;
        d.drawRect(fx, fy, 3, 3, SH110X_WHITE);
        d.drawPixel(fx + 1, fy - 1, SH110X_WHITE);
    }
    if (bonusX >= 0) {
        uint32_t left = bonusUntil - now;
        if (left > 1500 || (now / 100) % 2) {
            int fx = X0 + bonusX * CELL, fy = Y0 + bonusY * CELL;
            d.fillRect(fx - 1, fy - 1, 5, 5, SH110X_WHITE);
            d.drawPixel(fx + 1, fy + 1, SH110X_BLACK);
        }
        d.drawFastHLine(44, 3, (int)(40 * left / 5000), SH110X_WHITE);
    }

    bool visible = !(phase == P_DEAD && (now - phaseAt) / 150 % 2);
    if (visible) drawBody(d, X0, Y0, bx, by, len, dx, dy);

    if (phase == P_READY && (now / 400) % 2 == 0) GameUi::centerText(d, "Toca una flecha", 46);
    if (phase == P_PAUSE) GameUi::drawPause(d, pauseSel);
    if (phase == P_OVER) GameUi::drawOver(d, "TE CHOCASTE!", String(score), "Record " + String(best), newRecord, now, phaseAt);
}

void Snake::drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {
    // a snake going round a rectangle
    static uint8_t xs[12], ys[12];
    const int W = 14, H = 4, P = 2 * (W + H);
    int head = (now / 110) % P;
    for (int i = 0; i < 12; i++) {
        int p = (head - i + P) % P;
        int x, y;
        if (p < W) { x = p; y = 0; }
        else if (p < W + H) { x = W; y = p - W; }
        else if (p < 2 * W + H) { x = W - (p - W - H); y = H; }
        else { x = 0; y = H - (p - 2 * W - H); }
        xs[i] = x;
        ys[i] = y;
    }
    int8_t hdx = (int8_t)xs[0] - (int8_t)xs[1], hdy = (int8_t)ys[0] - (int8_t)ys[1];
    drawBody(d, 34 + ox, 32, xs, ys, 12, hdx, hdy);
    d.drawRect(34 + 7 * 4 + ox, 32 + 2 * 4, 3, 3, SH110X_WHITE);
}
