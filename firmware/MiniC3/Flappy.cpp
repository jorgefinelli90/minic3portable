#include "Flappy.h"

#include <Adafruit_SH110X.h>
#include <Preferences.h>

#include "MiniC3.h"

static const int BIRD_X = 26;
static const int GROUND = 57;
static const int PIPE_W = 12;
static const int SPACING = 58;
static const float GRAVITY = 380.0f;
static const float FLAP_V = -96.0f;     // each flap lifts it ~12 px
static const float MAX_FALL = 150.0f;

void Flappy::begin() {
    Preferences p;
    p.begin("minic3_game", true);
    best = p.getUInt("fla_hi", 0);
    p.end();
}

void Flappy::saveRecord() {
    Preferences p;
    p.begin("minic3_game", false);
    p.putUInt("fla_hi", best);
    p.end();
}

void Flappy::led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms, uint32_t now) {
#if MINI_LED_RGB_ORDER
    rgbLedWrite(MINI_RGB_LED_PIN, g, r, b);   // the C3-Zero LED takes RGB
#else
    rgbLedWrite(MINI_RGB_LED_PIN, r, g, b);
#endif
    ledOn = true;
    ledUntil = now + ms;
}

void Flappy::ledOff() {
    if (!ledOn) return;
    rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);
    ledOn = false;
}

float Flappy::speed() const {
    return min(62.0f, 40.0f + score * 0.8f);
}

int Flappy::gap() const {
    return max(20, 27 - (int)score / 5);
}

void Flappy::start(uint32_t now) {
    y = 26;
    vy = 0;
    score = 0;
    newRecord = false;
    scroll = 0;
    for (Pipe& p : pipes) p.alive = false;
    for (Feather& f : feathers) f.alive = false;
    prev = {true, true, true, true, true};   // keys must be released first
    lastFrame = now;
    phase = P_READY;
    phaseAt = now;
}

void Flappy::pause() {
    if (phase != P_PLAY && phase != P_READY) return;
    pausedFrom = phase;
    phase = P_PAUSE;
    phaseAt = millis();
    pauseSel = 0;
    ledOff();
}

void Flappy::flap(uint32_t now) {
    vy = FLAP_V;
    flapAt = now;
}

void Flappy::die(uint32_t now) {
    phase = P_DEAD;
    phaseAt = now;
    shakeUntil = now + 300;
    vy = min(vy, 0.0f);
    led(40, 0, 0, 400, now);
    // feathers
    for (Feather& f : feathers) {
        f.x = BIRD_X + 4 + random(-2, 3);
        f.y = y + 3;
        f.vx = random(-50, 51);
        f.vy = random(-80, 10);
        f.life = 400 + random(0, 400);
        f.alive = true;
    }
    newRecord = score > best;
    if (newRecord) {
        best = score;
        saveRecord();
    }
}

void Flappy::frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) {
    if (phase == P_OFF) return;
    uint32_t gapMs = now - lastFrame;
    float dt = (gapMs > 200) ? 0.0f : min(gapMs, (uint32_t)40) / 1000.0f;
    lastFrame = now;
    if (ledOn && (int32_t)(now - ledUntil) >= 0) ledOff();

    bool eUp = k.up && !prev.up, eDown = k.down && !prev.down;
    bool eLeft = k.left && !prev.left, eA = k.a && !prev.a;
    prev = k;

    for (Feather& f : feathers) {
        if (!f.alive) continue;
        f.vy += 150 * dt;
        f.x += f.vx * dt;
        f.y += f.vy * dt;
        uint16_t ms = (uint16_t)(dt * 1000);
        if (f.life <= ms || f.y > 63) f.alive = false;
        else f.life -= ms;
    }

    switch (phase) {
        case P_PAUSE:
            if (eUp || eDown) pauseSel ^= 1;
            if (eA) {
                if (pauseSel == 0) {
                    phase = pausedFrom;
                } else {
                    if (score > best) {
                        best = score;
                        saveRecord();
                    }
                    ledOff();
                    phase = P_OFF;
                }
            }
            break;

        case P_READY:
            y = 26 + sinf(now / 250.0f) * 3;
            scroll += 30 * dt;
            if (eDown) pause();
            else if (eA || eUp) {
                phase = P_PLAY;
                phaseAt = now;
                flap(now);
            }
            break;

        case P_PLAY: {
            if (eDown) {
                pause();
                break;
            }
            if (eA || eUp) flap(now);
            vy = min(vy + GRAVITY * dt, MAX_FALL);
            y += vy * dt;
            if (y < 0) {
                y = 0;
                vy = 0;
            }
            float sp = speed();
            scroll += sp * dt;

            // columns: move, new ones, points
            float rightmost = -1000;
            for (Pipe& p : pipes) {
                if (!p.alive) continue;
                p.x -= sp * dt;
                if (p.x < -PIPE_W) p.alive = false;
                else rightmost = max(rightmost, p.x);
                if (p.alive && !p.scored && p.x + PIPE_W < BIRD_X) {
                    p.scored = true;
                    score++;
                    led(12, 12, 12, 40, now);
                }
            }
            if (rightmost < 128 - SPACING) {
                for (Pipe& p : pipes) {
                    if (p.alive) continue;
                    int g = gap();
                    p.x = 128;
                    p.gapY = random(8 + g / 2, GROUND - 6 - g / 2 + 1);
                    p.scored = false;
                    p.alive = true;
                    break;
                }
            }

            // hits (a slightly forgiving box)
            int bx0 = BIRD_X + 1, bx1 = BIRD_X + 8, by0 = (int)y + 1, by1 = (int)y + 6;
            if (by1 >= GROUND) {
                y = GROUND - 7;
                die(now);
                break;
            }
            for (Pipe& p : pipes) {
                if (!p.alive) continue;
                int g = gap();
                int top = p.gapY - g / 2, bottom = p.gapY + g / 2;
                if (bx1 >= (int)p.x && bx0 <= (int)p.x + PIPE_W - 1 && (by0 < top || by1 > bottom)) {
                    die(now);
                    break;
                }
            }
            break;
        }

        case P_DEAD:
            vy = min(vy + GRAVITY * dt, MAX_FALL);
            y = min(y + vy * dt, (float)(GROUND - 7));
            if (now - phaseAt > 900) {
                phase = P_OVER;
                phaseAt = now;
            }
            break;

        case P_OVER:
            if (now - phaseAt > 700) {
                if (eA) start(now);
                else if (eLeft) {
                    ledOff();
                    phase = P_OFF;
                }
            }
            break;

        default:
            break;
    }

    if (phase != P_OFF) draw(d, now);
}

// ---------------------------------------------------------------------------

void Flappy::drawBird(Adafruit_GFX& d, int x, int y, bool wingUp, bool dead) {
    d.fillCircle(x + 4, y + 3, 3, SH110X_WHITE);
    // beak
    d.drawFastHLine(x + 8, y + 3, 2, SH110X_WHITE);
    d.drawPixel(x + 8, y + 4, SH110X_WHITE);
    // eye
    if (dead) {
        d.drawPixel(x + 5, y + 1, SH110X_BLACK);
        d.drawPixel(x + 6, y + 2, SH110X_BLACK);
    } else {
        d.drawPixel(x + 5, y + 2, SH110X_BLACK);
    }
    // wing
    if (wingUp) d.drawLine(x + 1, y + 3, x + 3, y + 1, SH110X_BLACK);
    else d.drawLine(x + 1, y + 3, x + 3, y + 5, SH110X_BLACK);
}

static void bricks(Adafruit_GFX& d, int x, int y0, int y1) {
    if (y1 <= y0) return;
    d.fillRect(x, y0, PIPE_W, y1 - y0, SH110X_BLACK);
    d.drawRect(x, y0, PIPE_W, y1 - y0, SH110X_WHITE);
    int row = 0;
    for (int yy = y0 + 4; yy < y1 - 1; yy += 4, row++) {
        d.drawFastHLine(x, yy, PIPE_W, SH110X_WHITE);
        int jx = x + ((row & 1) ? 4 : 8);
        d.drawFastVLine(jx, yy - 3, 3, SH110X_WHITE);
    }
}

void Flappy::drawColumn(Adafruit_GFX& d, int x, int gapTop, int gapBottom, int ground, int sky) {
    bricks(d, x, sky, gapTop);
    bricks(d, x, gapBottom + 1, ground);
}

void Flappy::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    int ox = 0, oy = 0;
    if ((int32_t)(shakeUntil - now) > 0) {
        ox = random(-2, 3);
        oy = random(-1, 2);
    }

    // far away dots, slower than the columns
    for (int i = 0; i < 12; i++) {
        int sx = ((i * 41 - (int)(scroll * 0.25f)) % 128 + 128) % 128;
        int sy = 3 + (i * 17) % 44;
        d.drawPixel(sx + ox, sy + oy, SH110X_WHITE);
    }

    int g = gap();
    for (Pipe& p : pipes) {
        if (!p.alive) continue;
        drawColumn(d, (int)p.x + ox, p.gapY - g / 2 + oy, p.gapY + g / 2 + oy, GROUND + oy);
    }

    // ground
    d.drawFastHLine(0, GROUND + oy, 128, SH110X_WHITE);
    int off = (int)scroll % 6;
    for (int x = -off; x < 128; x += 6) d.drawLine(x + ox, 63, x + 5 + ox, GROUND + 1 + oy, SH110X_WHITE);

    bool wingUp = (phase == P_PLAY) ? (now - flapAt < 140 || (now / 160) % 2) : (now / 200) % 2;
    drawBird(d, BIRD_X + ox, (int)y + oy, wingUp && phase != P_DEAD && phase != P_OVER, phase == P_DEAD || phase == P_OVER);

    for (Feather& f : feathers) {
        if (f.alive) d.drawPixel((int)f.x, (int)f.y, SH110X_WHITE);
    }

    // score
    if (phase == P_PLAY || phase == P_DEAD || phase == P_PAUSE) {
        String s = String(score);
        int w = s.length() * 12 - 2;
        int x = (128 - w) / 2;
        d.fillRect(x - 2, 1, w + 4, 16, SH110X_BLACK);
        d.setTextSize(2);
        d.setCursor(x, 2);
        d.print(s);
        d.setTextSize(1);
    }

    if (phase == P_READY) {
        if ((now / 450) % 2 == 0) {
            d.fillRect(40, 41, 50, 10, SH110X_BLACK);
            d.setCursor(42, 42);
            d.print("A: volar");
        }
        if (best > 0) {
            String r = "Record " + String(best);
            d.fillRect(0, 0, r.length() * 6 + 2, 9, SH110X_BLACK);
            d.setCursor(1, 1);
            d.print(r);
        }
    }

    if (phase == P_PAUSE) {
        d.fillRect(26, 18, 76, 34, SH110X_BLACK);
        d.drawRect(26, 18, 76, 34, SH110X_WHITE);
        d.fillRect(26, 18, 76, 10, SH110X_WHITE);
        d.setTextColor(SH110X_BLACK);
        d.setCursor(49, 19);
        d.print("PAUSA");
        const char* opts[2] = {"Seguir", "Salir"};
        for (int i = 0; i < 2; i++) {
            int yy = 31 + i * 10;
            if (i == pauseSel) {
                d.fillRect(28, yy - 1, 72, 9, SH110X_WHITE);
                d.setTextColor(SH110X_BLACK);
            } else {
                d.setTextColor(SH110X_WHITE);
            }
            d.setCursor(34, yy);
            d.print(i == pauseSel ? "> " : "  ");
            d.print(opts[i]);
        }
        d.setTextColor(SH110X_WHITE);
    }

    if (phase == P_OVER) {
        d.fillRect(6, 6, 116, 50, SH110X_BLACK);
        d.drawRect(6, 6, 116, 50, SH110X_WHITE);
        d.setCursor(35, 9);
        d.print("TE CAISTE!");
        String s = String(score);
        d.setTextSize(2);
        int sy = 19;
        if (now - phaseAt < 300) sy = 19 - (int)(8 * (1.0f - (now - phaseAt) / 300.0f));
        d.setCursor((128 - (int)s.length() * 12 + 2) / 2, sy);
        d.print(s);
        d.setTextSize(1);
        if (newRecord) {
            if ((now / 300) % 2 == 0) {
                d.setCursor(25, 36);
                d.print("NUEVO RECORD!");
            }
        } else {
            String r = "Record " + String(best);
            d.setCursor((128 - (int)r.length() * 6 + 1) / 2, 36);
            d.print(r);
        }
        if (now - phaseAt > 700) {
            d.setCursor(14, 46);
            d.print("A otra vez \x1b menu");
        }
    }
}

// Menu demo: the bird bobbing between brick columns that go by
void Flappy::drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {
    // below the title: columns going by and the bird in the gap
    for (int i = 0; i < 3; i++) {
        int x = 128 - (int)((now / 33 + i * 50) % 150) + ox;
        int gy = 41 + ((i * 7) % 3 - 1) * 2;
        drawColumn(d, x, gy - 7, gy + 7, 52, 30);
    }
    d.drawFastHLine(0 + ox, 52, 128, SH110X_WHITE);
    int by = 37 + (int)(sinf(now / 220.0f) * 2);
    drawBird(d, 26 + ox, by, (now / 160) % 2);

}
