#include "Invaders.h"

#include <Adafruit_SH110X.h>

static const int SX = 13, SY = 8;           // formation spacing
static const int PLAYER_Y = 57;
static const int BUNKER_Y = 46;
static const int BUNKER_X[3] = {16, 57, 98};

// 8x6 aliens, two frames each (bit 7 = left pixel). Original drawings.
static const uint8_t ALIENS[3][2][6] = {
    {{0x3C, 0x42, 0x99, 0x99, 0x42, 0xA5}, {0x3C, 0x42, 0x99, 0x99, 0x42, 0x5A}},   // eye
    {{0x3C, 0x7E, 0xE7, 0x24, 0x42, 0x00}, {0x3C, 0x7E, 0xE7, 0x24, 0x24, 0x66}},   // mushroom
    {{0x18, 0x3C, 0x66, 0xDB, 0x24, 0x5A}, {0x18, 0x3C, 0x66, 0xDB, 0x24, 0xA5}},   // diamond
};
static const uint8_t BOOM_SPR[6] = {0x24, 0x99, 0x42, 0x42, 0x99, 0x24};
static const uint16_t SHIP[5] = {0x010, 0x038, 0x07C, 0x1FF, 0x1FF};             // 9 wide
static const uint16_t UFO[4] = {0x0F8, 0x3FE, 0x6AB, 0x18C};                      // 11 wide
static const uint16_t BUNKER[5] = {0x0FFC, 0x1FFE, 0x3FFF, 0x3C0F, 0x3807};      // 14 wide
static const int ROW_TYPE[4] = {0, 1, 1, 2};
static const int ROW_POINTS[4] = {30, 20, 20, 10};

static void sprite(Adafruit_GFX& d, int x, int y, const uint8_t* rows, int h) {
    for (int r = 0; r < h; r++) {
        for (int b = 0; b < 8; b++) {
            if (rows[r] & (0x80 >> b)) d.drawPixel(x + b, y + r, SH110X_WHITE);
        }
    }
}

static void sprite16(Adafruit_GFX& d, int x, int y, const uint16_t* rows, int h, int w) {
    for (int r = 0; r < h; r++) {
        for (int b = 0; b < w; b++) {
            if (rows[r] & (1 << (w - 1 - b))) d.drawPixel(x + b, y + r, SH110X_WHITE);
        }
    }
}

void Invaders::start(uint32_t now) {
    score = 0;
    lives = 3;
    wave = 1;
    newRecord = false;
    prev = {true, true, true, true, true};
    for (int i = 0; i < 3; i++) memcpy(bunker[i], BUNKER, sizeof(BUNKER));
    newWave(now);
}

void Invaders::newWave(uint32_t now) {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) alive[r][c] = true;
    }
    aliveCount = ROWS * COLS;
    fx = 4;
    fy = 12 + min(wave - 1, 4) * 2;
    fdir = 1;
    px = 59;
    bullet.alive = false;
    for (Shot& b : bombs) b.alive = false;
    for (Boom& b : booms) b.at = 0;
    ufoOn = false;
    ufoAt = now + random(12000, 22000);
    bombAt = now + 1500;
    phase = P_WAVE;
    phaseAt = now;
    lastFrame = now;
}

void Invaders::pause() {
    if (phase != P_PLAY && phase != P_WAVE) return;
    phase = P_PAUSE;
    pauseSel = 0;
    led.off();
}

void Invaders::addBoom(int x, int y, uint32_t now) {
    Boom* slot = &booms[0];
    for (Boom& b : booms) {
        if (b.at == 0 || now - b.at > 200) { slot = &b; break; }
    }
    slot->x = x;
    slot->y = y;
    slot->at = now;
}

// A shot (x, y .. y+h) against the shelters: wears a little bit away
bool Invaders::hitBunker(float x, float y, int h) {
    int ix = (int)x;
    for (int i = 0; i < 3; i++) {
        int bx = ix - BUNKER_X[i];
        if (bx < 0 || bx >= 14) continue;
        for (int dy = 0; dy < h; dy++) {
            int by = (int)y + dy - BUNKER_Y;
            if (by < 0 || by >= 5) continue;
            if (bunker[i][by] & (1 << (13 - bx))) {
                for (int ey = by - 1; ey <= by + 1; ey++) {
                    if (ey < 0 || ey >= 5) continue;
                    for (int ex = bx - 1; ex <= bx + 1; ex++) {
                        if (ex >= 0 && ex < 14 && random(0, 4)) bunker[i][ey] &= ~(1 << (13 - ex));
                    }
                }
                return true;
            }
        }
    }
    return false;
}

void Invaders::gameOver(uint32_t now) {
    newRecord = score > best;
    if (newRecord) {
        best = score;
        GameUi::saveRecord("inv_hi", best);
    }
    led.set(40, 0, 0, 500, now);
    phase = P_OVER;
    phaseAt = now;
}

void Invaders::frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) {
    GameKeys e = GameUi::edges(k, prev);
    led.tick(now);
    uint32_t gap = now - lastFrame;
    float dt = gap > 200 ? 0.0f : min(gap, (uint32_t)40) / 1000.0f;
    lastFrame = now;

    switch (phase) {
        case P_PAUSE: {
            int r = GameUi::pauseInput(e, pauseSel);
            if (r == 1) {
                phase = P_PLAY;
                lastFrame = now;
            } else if (r == 2) {
                if (score > best) {
                    best = score;
                    GameUi::saveRecord("inv_hi", best);
                }
                phase = P_OFF;
                return;
            }
            break;
        }
        case P_WAVE:
            if (e.down) {
                pause();
                break;
            }
            if (now - phaseAt > 1300) {
                phase = P_PLAY;
                marchAt = now;
            }
            break;
        case P_HIT:
            if (now - phaseAt > 1100) {
                if (lives <= 0) gameOver(now);
                else {
                    phase = P_PLAY;
                    px = 59;
                }
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
        case P_PLAY: {
            if (e.down) {
                pause();
                break;
            }
            // ship and fire
            int dir = (k.right ? 1 : 0) - (k.left ? 1 : 0);
            px = constrain(px + dir * 70.0f * dt, 1.0f, 118.0f);
            if ((e.a || e.up) && !bullet.alive) {
                bullet = {px + 4, (float)PLAYER_Y - 3, true};
            }

            // the formation marches (faster when fewer are left)
            uint32_t interval = 40 + aliveCount * 17;
            if ((int32_t)(now - marchAt) >= 0) {
                marchAt = now + interval;
                frameAlt = !frameAlt;
                int minC = COLS, maxC = -1, maxR = -1;
                for (int r = 0; r < ROWS; r++) {
                    for (int c = 0; c < COLS; c++) {
                        if (!alive[r][c]) continue;
                        minC = min(minC, c);
                        maxC = max(maxC, c);
                        maxR = max(maxR, r);
                    }
                }
                int left = fx + minC * SX, right = fx + maxC * SX + 8;
                if ((fdir > 0 && right + 2 > 127) || (fdir < 0 && left - 2 < 0)) {
                    fy += 3;
                    fdir = -fdir;
                } else {
                    fx += 2 * fdir;
                }
                // they wear the shelters away as they go through
                for (int r = 0; r < ROWS; r++) {
                    for (int c = 0; c < COLS; c++) {
                        if (!alive[r][c]) continue;
                        int ay = fy + r * SY;
                        if (ay + 6 < BUNKER_Y || ay > BUNKER_Y + 4) continue;
                        for (int i = 0; i < 3; i++) {
                            for (int x = 0; x < 8; x++) {
                                int bx = fx + c * SX + x - BUNKER_X[i];
                                if (bx < 0 || bx >= 14) continue;
                                for (int y = 0; y < 6; y++) {
                                    int by = ay + y - BUNKER_Y;
                                    if (by >= 0 && by < 5) bunker[i][by] &= ~(1 << (13 - bx));
                                }
                            }
                        }
                    }
                }
                if (fy + maxR * SY + 6 >= PLAYER_Y) {   // they landed
                    lives = 0;
                    addBoom((int)px, PLAYER_Y - 1, now);
                    phase = P_HIT;
                    phaseAt = now;
                    led.set(40, 0, 0, 500, now);
                    break;
                }
            }

            // bullet
            if (bullet.alive) {
                bullet.y -= 140.0f * dt;
                if (bullet.y < 9) bullet.alive = false;
                else if (hitBunker(bullet.x, bullet.y, 3)) bullet.alive = false;
                else if (ufoOn && bullet.y < 14 && bullet.x >= ufoX && bullet.x <= ufoX + 10) {
                    bullet.alive = false;
                    ufoOn = false;
                    score += 50 * random(1, 4);
                    addBoom((int)ufoX + 1, 9, now);
                    led.set(30, 0, 30, 200, now);
                } else {
                    for (int r = ROWS - 1; r >= 0 && bullet.alive; r--) {
                        for (int c = 0; c < COLS && bullet.alive; c++) {
                            if (!alive[r][c]) continue;
                            int ax = fx + c * SX, ay = fy + r * SY;
                            if (bullet.x >= ax && bullet.x <= ax + 7 && bullet.y <= ay + 5 && bullet.y + 3 >= ay) {
                                alive[r][c] = false;
                                aliveCount--;
                                bullet.alive = false;
                                score += ROW_POINTS[r];
                                addBoom(ax, ay, now);
                                led.set(15, 15, 15, 40, now);
                            }
                        }
                    }
                }
            }

            // bombs: from the lowest alien of a random column
            if ((int32_t)(now - bombAt) >= 0) {
                bombAt = now + random(500, 1100) - min(wave * 60, 300);
                int c = random(0, COLS);
                for (int tries = 0; tries < COLS; tries++, c = (c + 1) % COLS) {
                    int r = ROWS - 1;
                    while (r >= 0 && !alive[r][c]) r--;
                    if (r < 0) continue;
                    for (Shot& b : bombs) {
                        if (b.alive) continue;
                        b = {(float)(fx + c * SX + 4), (float)(fy + r * SY + 6), true};
                        break;
                    }
                    break;
                }
            }
            for (Shot& b : bombs) {
                if (!b.alive) continue;
                b.y += (45.0f + wave * 5) * dt;
                if (b.y > 63) b.alive = false;
                else if (hitBunker(b.x, b.y, 4)) b.alive = false;
                else if (b.y + 4 >= PLAYER_Y && b.y <= PLAYER_Y + 4 && b.x >= px && b.x <= px + 8) {
                    b.alive = false;
                    lives--;
                    addBoom((int)px, PLAYER_Y - 1, now);
                    led.set(40, 0, 0, 400, now);
                    for (Shot& o : bombs) o.alive = false;
                    bullet.alive = false;
                    phase = P_HIT;
                    phaseAt = now;
                    break;
                }
            }

            // the saucer
            if (!ufoOn && (int32_t)(now - ufoAt) >= 0) {
                ufoOn = true;
                ufoDir = random(0, 2) ? 1 : -1;
                ufoX = ufoDir > 0 ? -11 : 128;
            }
            if (ufoOn) {
                ufoX += ufoDir * 30.0f * dt;
                if (ufoX < -12 || ufoX > 129) {
                    ufoOn = false;
                    ufoAt = now + random(15000, 25000);
                }
            }

            if (aliveCount == 0) {
                wave++;
                score += 100;
                led.set(30, 26, 0, 300, now);
                newWave(now);
            }
            break;
        }
        default:
            break;
    }
    draw(d, now);
}

void Invaders::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    d.setCursor(0, 0);
    d.print(score);
    for (int i = 0; i < lives - (phase == P_HIT ? 0 : 1) && i < 5; i++) {
        d.fillRect(122 - i * 8, 3, 6, 2, SH110X_WHITE);
        d.drawPixel(124 - i * 8, 2, SH110X_WHITE);
    }

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (alive[r][c]) sprite(d, fx + c * SX, fy + r * SY, ALIENS[ROW_TYPE[r]][frameAlt], 6);
        }
    }
    for (Boom& b : booms) {
        if (b.at && now - b.at < 200) sprite(d, b.x, b.y, BOOM_SPR, 6);
    }
    for (int i = 0; i < 3; i++) sprite16(d, BUNKER_X[i], BUNKER_Y, bunker[i], 5, 14);
    if (ufoOn) sprite16(d, (int)ufoX, 9, UFO, 4, 11);

    bool showShip = !(phase == P_HIT || (phase == P_OVER && lives <= 0));
    if (showShip) sprite16(d, (int)px, PLAYER_Y, SHIP, 5, 9);
    d.drawFastHLine(0, 63, 128, SH110X_WHITE);

    if (bullet.alive) d.drawFastVLine((int)bullet.x, (int)bullet.y, 3, SH110X_WHITE);
    for (Shot& b : bombs) {
        if (!b.alive) continue;
        int x = (int)b.x, y = (int)b.y;
        bool z = (now / 80) % 2;
        d.drawPixel(x + (z ? 0 : 1), y, SH110X_WHITE);
        d.drawPixel(x + (z ? 1 : 0), y + 1, SH110X_WHITE);
        d.drawPixel(x + (z ? 0 : 1), y + 2, SH110X_WHITE);
        d.drawPixel(x + (z ? 1 : 0), y + 3, SH110X_WHITE);
    }

    if (phase == P_WAVE) {
        d.fillRect(34, 30, 60, 11, SH110X_BLACK);
        GameUi::centerText(d, "OLEADA " + String(wave), 32);
    }
    if (phase == P_PAUSE) GameUi::drawPause(d, pauseSel);
    if (phase == P_OVER) GameUi::drawOver(d, "INVADIDOS!", String(score), "Record " + String(best), newRecord, now, phaseAt);
}

void Invaders::drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {
    // a row of aliens marching and the ship shooting at them
    bool alt = (now / 400) % 2;
    int shift = (int)((now / 400) % 8);
    shift = shift < 4 ? shift * 2 : (8 - shift) * 2;
    for (int c = 0; c < 6; c++) sprite(d, 24 + c * SX + shift + ox, 31, ALIENS[c % 3][alt], 6);
    int sx = 50 + (int)(sinf(now / 600.0f) * 20);
    sprite16(d, sx + ox, 46, SHIP, 5, 9);
    int by = 44 - (int)((now / 20) % 10);
    d.drawFastVLine(sx + 4 + ox, by, 3, SH110X_WHITE);
}
