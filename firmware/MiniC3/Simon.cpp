#include "Simon.h"

#include <Adafruit_SH110X.h>

// pads: 0 up, 1 right, 2 down, 3 left (around the center 64, 37)
static const uint8_t PAD_RGB[4][3] = {{0, 40, 0}, {40, 0, 0}, {35, 28, 0}, {0, 0, 45}};
static const uint32_t GAP_MS = 160;

void Simon::start(uint32_t now) {
    len = 1;
    seq[0] = random(0, 4);
    pos = 0;
    lit = -1;
    newRecord = false;
    prev = {true, true, true, true, true};
    phase = P_SHOW;
    phaseAt = now + 700;   // a moment before it starts
}

void Simon::pause() {
    if (phase != P_SHOW && phase != P_INPUT) return;
    pausedFrom = phase;
    phase = P_PAUSE;
    pauseSel = 0;
    lit = -1;
    led.off();
}

uint32_t Simon::onMs() const {
    return max(220, 560 - len * 22);
}

void Simon::light(int pad, uint32_t ms, uint32_t now) {
    lit = pad;
    litUntil = now + ms;
    led.set(PAD_RGB[pad][0], PAD_RGB[pad][1], PAD_RGB[pad][2], ms, now);
}

void Simon::frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) {
    GameKeys e = GameUi::edges(k, prev);
    led.tick(now);
    if (lit >= 0 && (int32_t)(now - litUntil) >= 0) lit = -1;

    switch (phase) {
        case P_PAUSE: {
            int r = GameUi::pauseInput(e, pauseSel);
            if (r == 1) {
                phase = pausedFrom;
                if (phase == P_SHOW) pos = 0;   // shows it again from the start
                phaseAt = now + 500;
            } else if (r == 2) {
                phase = P_OFF;
                return;
            }
            break;
        }
        case P_SHOW:
            if (e.a) {
                pause();
                break;
            }
            // one pad after the other: lit onMs, then a short gap
            if ((int32_t)(now - phaseAt) >= 0 && lit < 0) {
                if (pos < len) {
                    light(seq[pos], onMs(), now);
                    pos++;
                    phaseAt = now + onMs() + GAP_MS;
                } else {
                    phase = P_INPUT;
                    pos = 0;
                    phaseAt = now;
                }
            }
            break;
        case P_INPUT: {
            if (e.a) {
                pause();
                break;
            }
            int pad = e.up ? 0 : e.right ? 1 : e.down ? 2 : e.left ? 3 : -1;
            if (pad < 0) break;
            if (pad != seq[pos]) {
                lit = -1;
                led.set(50, 0, 0, 700, now);
                phase = P_FAIL;
                phaseAt = now;
                uint32_t done = len - 1;
                if (done > best) {
                    newRecord = true;
                    best = done;
                    GameUi::saveRecord("sim_hi", best);
                }
                break;
            }
            light(pad, 220, now);
            pos++;
            if (pos >= len) {
                phase = P_GOOD;
                phaseAt = now;
            }
            break;
        }
        case P_GOOD:
            if (now - phaseAt > 750) {
                if (len < MAX_LEN) seq[len++] = random(0, 4);
                if ((uint32_t)(len - 1) > best) {
                    best = len - 1;
                    GameUi::saveRecord("sim_hi", best);
                    newRecord = true;
                }
                pos = 0;
                phase = P_SHOW;
                phaseAt = now + 300;
            }
            break;
        case P_FAIL:
            if (now - phaseAt > 1100) {
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

// Arrow-shaped pads; lit = filled
void Simon::drawPad(Adafruit_GFX& d, int pad, bool on, int ox, int oy) {
    const int cx = 64 + ox, cy = 37 + oy;
    int x0, y0, x1, y1, x2, y2;   // triangle
    int rx, ry, rw, rh;           // stem
    switch (pad) {
        case 0: x0 = cx; y0 = cy - 26; x1 = cx - 10; y1 = cy - 16; x2 = cx + 10; y2 = cy - 16; rx = cx - 4; ry = cy - 16; rw = 9; rh = 8; break;
        case 2: x0 = cx; y0 = cy + 26; x1 = cx - 10; y1 = cy + 16; x2 = cx + 10; y2 = cy + 16; rx = cx - 4; ry = cy + 9; rw = 9; rh = 8; break;
        case 1: x0 = cx + 36; y0 = cy; x1 = cx + 24; y1 = cy - 11; x2 = cx + 24; y2 = cy + 11; rx = cx + 12; ry = cy - 4; rw = 13; rh = 9; break;
        default: x0 = cx - 36; y0 = cy; x1 = cx - 24; y1 = cy - 11; x2 = cx - 24; y2 = cy + 11; rx = cx - 24; ry = cy - 4; rw = 13; rh = 9; break;
    }
    if (on) {
        d.fillTriangle(x0, y0, x1, y1, x2, y2, SH110X_WHITE);
        d.fillRect(rx, ry, rw, rh, SH110X_WHITE);
    } else {
        d.drawTriangle(x0, y0, x1, y1, x2, y2, SH110X_WHITE);
        d.drawRect(rx, ry, rw, rh, SH110X_WHITE);
    }
}

void Simon::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    bool failFlash = (phase == P_FAIL && (now - phaseAt) / 120 % 2 == 0);
    for (int p = 0; p < 4; p++) drawPad(d, p, p == lit || failFlash, 0, 0);

    // center: how many so far
    String n = String(len);
    GameUi::centerText(d, n, 34);
    d.setCursor(0, 0);
    d.print("Nivel " + String(len));
    String r = "Rec " + String(best);
    d.setCursor(128 - (int)r.length() * 6 + 1, 0);
    d.print(r);
    d.setCursor(0, 56);
    if (phase == P_SHOW) d.print("Mira...");
    else if (phase == P_INPUT) d.print("Tu turno");
    else if (phase == P_GOOD) d.print("Bien!");

    if (phase == P_PAUSE) GameUi::drawPause(d, pauseSel);
    if (phase == P_OVER) GameUi::drawOver(d, "TE EQUIVOCASTE", String(len - 1), "Record " + String(best), newRecord, now, phaseAt);
}

void Simon::drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {
    // the four pads, small, lighting in turn
    int on = (now / 350) % 5;
    for (int p = 0; p < 4; p++) {
        // scaled-down pads: reuse the shape around a lower center
        int cx = 64 + ox, cy = 41;
        bool lit = (p == on);
        switch (p) {
            case 0: lit ? d.fillTriangle(cx, cy - 11, cx - 5, cy - 5, cx + 5, cy - 5, SH110X_WHITE) : d.drawTriangle(cx, cy - 11, cx - 5, cy - 5, cx + 5, cy - 5, SH110X_WHITE); break;
            case 2: lit ? d.fillTriangle(cx, cy + 11, cx - 5, cy + 5, cx + 5, cy + 5, SH110X_WHITE) : d.drawTriangle(cx, cy + 11, cx - 5, cy + 5, cx + 5, cy + 5, SH110X_WHITE); break;
            case 1: lit ? d.fillTriangle(cx + 17, cy, cx + 10, cy - 6, cx + 10, cy + 6, SH110X_WHITE) : d.drawTriangle(cx + 17, cy, cx + 10, cy - 6, cx + 10, cy + 6, SH110X_WHITE); break;
            default: lit ? d.fillTriangle(cx - 17, cy, cx - 10, cy - 6, cx - 10, cy + 6, SH110X_WHITE) : d.drawTriangle(cx - 17, cy, cx - 10, cy - 6, cx - 10, cy + 6, SH110X_WHITE); break;
        }
    }
}
