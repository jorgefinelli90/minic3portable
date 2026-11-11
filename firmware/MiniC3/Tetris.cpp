#include "Tetris.h"

#include <Adafruit_SH110X.h>
#include <Preferences.h>

#include "MiniC3.h"

static const int CELL = 4;
static const int BX = 44;                 // well: x 44..83, walls at 43 and 84
static const uint32_t CLEAR_MS = 380;
static const uint32_t LOCK_MS = 450;
static const uint32_t DAS_MS = 170;
static const uint32_t ARR_MS = 55;
static const uint32_t PAUSE_HOLD_MS = 650;

// I O T S Z J L
static const uint16_t SHAPES[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    {0x6600, 0x6600, 0x6600, 0x6600},
    {0x4E00, 0x4640, 0x0E40, 0x4C40},
    {0x6C00, 0x4620, 0x06C0, 0x8C40},
    {0xC600, 0x2640, 0x0C60, 0x4C80},
    {0x8E00, 0x6440, 0x0E20, 0x44C0},
    {0x2E00, 0x4460, 0x0E80, 0xC440},
};

uint16_t Tetris::shape(uint8_t t, uint8_t r) {
    return SHAPES[t % 7][r & 3];
}

static inline bool cellOn(uint16_t m, int r, int c) {
    return m & (0x8000 >> (r * 4 + c));
}

// ---------------------------------------------------------------------------

void Tetris::begin() {
    Preferences p;
    p.begin("minic3_game", true);
    best = p.getUInt("tet_hi", 0);
    p.end();
}

void Tetris::saveRecord() {
    Preferences p;
    p.begin("minic3_game", false);
    p.putUInt("tet_hi", best);
    p.end();
}

void Tetris::led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms, uint32_t now) {
#if MINI_LED_RGB_ORDER
    rgbLedWrite(MINI_RGB_LED_PIN, g, r, b);   // the C3-Zero LED takes RGB
#else
    rgbLedWrite(MINI_RGB_LED_PIN, r, g, b);
#endif
    ledOn = true;
    ledUntil = now + ms;
}

void Tetris::ledOff() {
    if (!ledOn) return;
    rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);
    ledOn = false;
}

uint8_t Tetris::draw7() {
    // 7-bag: every piece once, shuffled
    if (bagPos >= 7) {
        for (int i = 0; i < 7; i++) bag[i] = i;
        for (int i = 6; i > 0; i--) {
            int j = random(0, i + 1);
            uint8_t t = bag[i];
            bag[i] = bag[j];
            bag[j] = t;
        }
        bagPos = 0;
    }
    return bag[bagPos++];
}

void Tetris::start(uint32_t now) {
    memset(board, 0, sizeof(board));
    score = 0;
    lines = 0;
    level = 1;
    newRecord = false;
    bagPos = 7;
    nextType = draw7();
    prev = {true, true, true, true, true};   // keys must be released first
    aHoldUsed = true;
    dasDir = 0;
    phase = P_PLAY;
    phaseAt = now;
    spawn(now);
}

void Tetris::pause() {
    if (phase != P_PLAY && phase != P_CLEAR) return;
    pausedFrom = phase;
    phase = P_PAUSE;
    phaseAt = millis();
    pauseSel = 0;
    ledOff();
}

bool Tetris::collides(uint8_t t, uint8_t r, int x, int y) const {
    uint16_t m = shape(t, r);
    for (int rr = 0; rr < 4; rr++) {
        for (int cc = 0; cc < 4; cc++) {
            if (!cellOn(m, rr, cc)) continue;
            int bx = x + cc, by = y + rr;
            if (bx < 0 || bx >= COLS || by >= ROWS) return true;
            if (by >= 0 && board[by][bx]) return true;
        }
    }
    return false;
}

bool Tetris::spawn(uint32_t now) {
    type = nextType;
    nextType = draw7();
    rot = 0;
    px = 3;
    py = (type == 0) ? -1 : 0;   // the I lies on its second row
    fallAt = now + fallInterval();
    lockAt = 0;
    lockResets = 0;
    if (collides(type, rot, px, py)) {
        gameOver(now);
        return false;
    }
    return true;
}

uint32_t Tetris::fallInterval() const {
    float t = 750.0f;
    for (int i = 1; i < level; i++) t *= 0.84f;
    return max((uint32_t)60, (uint32_t)t);
}

bool Tetris::tryMove(int dx, int dy, uint32_t now) {
    if (collides(type, rot, px + dx, py + dy)) return false;
    px += dx;
    py += dy;
    // moved while resting on something: a little more time before it locks
    if (lockAt && lockResets < 10) {
        lockAt = now + LOCK_MS;
        lockResets++;
    }
    if (!collides(type, rot, px, py + 1)) lockAt = 0;
    return true;
}

void Tetris::rotate(uint32_t now) {
    uint8_t r = (rot + 1) & 3;
    static const int8_t KICKS[6][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {-2, 0}, {2, 0}};
    for (auto& k : KICKS) {
        if (!collides(type, r, px + k[0], py + k[1])) {
            rot = r;
            px += k[0];
            py += k[1];
            if (lockAt && lockResets < 10) {
                lockAt = now + LOCK_MS;
                lockResets++;
            }
            if (!collides(type, rot, px, py + 1)) lockAt = 0;
            return;
        }
    }
}

int Tetris::ghostY() const {
    int y = py;
    while (!collides(type, rot, px, y + 1)) y++;
    return y;
}

void Tetris::hardDrop(uint32_t now) {
    int y = ghostY();
    score += 2 * (y - py);
    py = y;
    lockPiece(now);
}

void Tetris::lockPiece(uint32_t now) {
    uint16_t m = shape(type, rot);
    bool above = false;
    for (int rr = 0; rr < 4; rr++) {
        for (int cc = 0; cc < 4; cc++) {
            if (!cellOn(m, rr, cc)) continue;
            int by = py + rr;
            if (by < 0) above = true;
            else board[by][px + cc] = type + 1;
        }
    }
    if (above) {
        gameOver(now);
        return;
    }

    clearMask = 0;
    for (int r = 0; r < ROWS; r++) {
        bool full = true;
        for (int c = 0; c < COLS && full; c++) full = board[r][c] != 0;
        if (full) clearMask |= (1 << r);
    }
    if (clearMask) {
        int n = __builtin_popcount(clearMask);
        if (n == 4) led(30, 26, 0, 400, now);
        else led(0, 15, 30, 150, now);
        phase = P_CLEAR;
        phaseAt = now;
    } else {
        spawn(now);
    }
}

void Tetris::finishClear(uint32_t now) {
    // keep the rows that are not full, from the bottom up
    uint8_t nb[ROWS][COLS];
    memset(nb, 0, sizeof(nb));
    int n = 0;
    int w = ROWS - 1;
    for (int r = ROWS - 1; r >= 0; r--) {
        if (clearMask & (1 << r)) {
            n++;
        } else {
            memcpy(nb[w], board[r], COLS);
            w--;
        }
    }
    memcpy(board, nb, sizeof(board));
    static const uint16_t POINTS[5] = {0, 100, 300, 500, 800};
    score += POINTS[min(n, 4)] * level;
    lines += n;
    level = min(99, 1 + lines / 10);
    clearMask = 0;
    phase = P_PLAY;
    phaseAt = now;
    spawn(now);
}

void Tetris::gameOver(uint32_t now) {
    newRecord = score > best;
    if (newRecord) {
        best = score;
        saveRecord();
    }
    led(40, 0, 0, 500, now);
    phase = P_FILL;
    phaseAt = now;
}

// ---------------------------------------------------------------------------

void Tetris::frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) {
    if (phase == P_OFF) return;
    if (ledOn && (int32_t)(now - ledUntil) >= 0) ledOff();

    bool eUp = k.up && !prev.up, eDown = k.down && !prev.down;
    bool eLeft = k.left && !prev.left, eRight = k.right && !prev.right;
    bool eA = k.a && !prev.a;
    if (eA) {
        aSince = now;
        aHoldUsed = false;
    }

    switch (phase) {
        case P_PAUSE:
            if (eUp || eDown) pauseSel ^= 1;
            if (eA) {
                if (pauseSel == 0) {
                    phase = pausedFrom;
                    fallAt = now + fallInterval();
                    if (lockAt) lockAt = now + LOCK_MS;
                    phaseAt = now;
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

        case P_CLEAR:
            if (now - phaseAt >= CLEAR_MS) finishClear(now);
            break;

        case P_FILL:
            if (now - phaseAt >= ROWS * 45 + 300) {
                phase = P_OVER;
                phaseAt = now;
            }
            break;

        case P_OVER:
            if (now - phaseAt > 800) {
                if (eA) start(now);
                else if (eLeft) {
                    ledOff();
                    phase = P_OFF;
                }
            }
            break;

        case P_PLAY: {
            // A held: pause
            if (k.a && !aHoldUsed && now - aSince >= PAUSE_HOLD_MS) {
                aHoldUsed = true;
                pause();
                break;
            }
            if (eA) rotate(now);
            if (eUp) {
                hardDrop(now);
                break;
            }

            // ← → with auto repeat
            int dir = (k.right ? 1 : 0) - (k.left ? 1 : 0);
            if (eLeft || eRight) {
                dasDir = eRight ? 1 : -1;
                dasAt = now + DAS_MS;
                tryMove(dasDir, 0, now);
            } else if (dir != 0 && dir == dasDir && (int32_t)(now - dasAt) >= 0) {
                tryMove(dir, 0, now);
                dasAt = now + ARR_MS;
            } else if (dir == 0) {
                dasDir = 0;
            }

            // falling (faster with ↓)
            uint32_t interval = k.down ? min((uint32_t)35, fallInterval()) : fallInterval();
            if (eDown) fallAt = now;
            if ((int32_t)(now - fallAt) >= 0) {
                fallAt = now + interval;
                if (!collides(type, rot, px, py + 1)) {
                    py++;
                    if (k.down) score += 1;
                    lockAt = 0;
                } else if (!lockAt) {
                    lockAt = now + LOCK_MS;
                }
            }
            if (lockAt && (int32_t)(now - lockAt) >= 0) {
                if (collides(type, rot, px, py + 1)) lockPiece(now);
                else lockAt = 0;
            }
            break;
        }

        default:
            break;
    }

    prev = k;
    if (phase != P_OFF) draw(d, now);
}

// ---------------------------------------------------------------------------

static void block(Adafruit_GFX& d, int c, int r) {
    d.fillRect(BX + c * CELL, r * CELL, CELL - 1, CELL - 1, SH110X_WHITE);
}

void Tetris::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    // walls
    d.drawFastVLine(BX - 1, 0, 64, SH110X_WHITE);
    d.drawFastVLine(BX + COLS * CELL, 0, 64, SH110X_WHITE);

    // the stack (rows being cleared blink)
    for (int r = 0; r < ROWS; r++) {
        bool clearing = (phase == P_CLEAR || (phase == P_PAUSE && pausedFrom == P_CLEAR)) && (clearMask & (1 << r));
        if (clearing) {
            if (((now - phaseAt) / 70) % 2 == 0) d.fillRect(BX, r * CELL, COLS * CELL - 1, CELL - 1, SH110X_WHITE);
            continue;
        }
        for (int c = 0; c < COLS; c++) {
            if (board[r][c]) block(d, c, r);
        }
    }

    // the piece and where it would land (dots)
    if (phase == P_PLAY || phase == P_PAUSE) {
        uint16_t m = shape(type, rot);
        int gy = ghostY();
        for (int rr = 0; rr < 4; rr++) {
            for (int cc = 0; cc < 4; cc++) {
                if (!cellOn(m, rr, cc)) continue;
                int gr = gy + rr;
                if (gy != py && gr >= 0) d.drawPixel(BX + (px + cc) * CELL + 1, gr * CELL + 1, SH110X_WHITE);
                if (py + rr >= 0) block(d, px + cc, py + rr);
            }
        }
    }

    // game over: the well fills up from the bottom
    if (phase == P_FILL || phase == P_OVER) {
        int filled = (phase == P_OVER) ? ROWS : min((int)ROWS, (int)((now - phaseAt) / 45));
        for (int i = 0; i < filled; i++) {
            int r = ROWS - 1 - i;
            for (int c = 0; c < COLS; c++) {
                d.fillRect(BX + c * CELL, r * CELL, CELL - 1, CELL - 1, SH110X_BLACK);
                d.drawRect(BX + c * CELL, r * CELL, CELL - 1, CELL - 1, SH110X_WHITE);
            }
        }
    }

    // left: score, level, lines
    d.setCursor(0, 0);
    d.print("PUNTOS");
    d.setCursor(0, 9);
    d.print(min(score, (uint32_t)999999));
    d.setCursor(0, 22);
    d.print("NIVEL");
    d.setCursor(0, 31);
    d.print(level);
    d.setCursor(0, 44);
    d.print("LINEAS");
    d.setCursor(0, 53);
    d.print(lines);

    // right: next piece and record
    d.setCursor(90, 0);
    d.print("SIG");
    uint16_t nm = shape(nextType, 0);
    for (int rr = 0; rr < 4; rr++) {
        for (int cc = 0; cc < 4; cc++) {
            if (cellOn(nm, rr, cc)) d.fillRect(92 + cc * CELL, 11 + rr * CELL, CELL - 1, CELL - 1, SH110X_WHITE);
        }
    }
    d.setCursor(90, 34);
    d.print("REC");
    d.setCursor(90, 43);
    d.print(min(max(best, score), (uint32_t)999999));

    if (phase == P_PAUSE) {
        d.fillRect(26, 14, 76, 38, SH110X_BLACK);
        d.drawRect(26, 14, 76, 38, SH110X_WHITE);
        d.fillRect(26, 14, 76, 10, SH110X_WHITE);
        d.setTextColor(SH110X_BLACK);
        d.setCursor(49, 15);
        d.print("PAUSA");
        const char* opts[2] = {"Seguir", "Salir"};
        for (int i = 0; i < 2; i++) {
            int y = 28 + i * 11;
            if (i == pauseSel) {
                d.fillRect(28, y - 2, 72, 11, SH110X_WHITE);
                d.setTextColor(SH110X_BLACK);
            } else {
                d.setTextColor(SH110X_WHITE);
            }
            d.setCursor(34, y);
            d.print(i == pauseSel ? "> " : "  ");
            d.print(opts[i]);
        }
        d.setTextColor(SH110X_WHITE);
    }

    if (phase == P_OVER) {
        d.fillRect(6, 8, 116, 50, SH110X_BLACK);
        d.drawRect(6, 8, 116, 50, SH110X_WHITE);
        d.setCursor(29, 11);
        d.print("FIN DEL JUEGO");
        String s = String(score);
        d.setTextSize(2);
        d.setCursor((128 - (int)s.length() * 12 + 2) / 2, 21);
        d.print(s);
        d.setTextSize(1);
        if (newRecord) {
            if ((now / 300) % 2 == 0) {
                d.setCursor(25, 38);
                d.print("NUEVO RECORD!");
            }
        } else {
            String r = "Record " + String(best);
            d.setCursor((128 - (int)r.length() * 6 + 1) / 2, 38);
            d.print(r);
        }
        if (now - phaseAt > 800) {
            d.setCursor(14, 48);
            d.print("A otra vez \x1b menu");
        }
    }
}

// Menu demo: a piece falls (turning) into the gap, two rows complete, flash and go
void Tetris::drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {
    const int C = 3, X0 = 49 + ox, Y0 = 31;
    d.drawFastVLine(X0 - 1, Y0 - 1, 22, SH110X_WHITE);
    d.drawFastVLine(X0 + 30, Y0 - 1, 22, SH110X_WHITE);
    d.drawFastHLine(X0 - 1, Y0 + 21, 32, SH110X_WHITE);
    uint32_t ph = now % 2800;
    auto cell = [&](int c, int r) { d.fillRect(X0 + c * C, Y0 + r * C, C - 1, C - 1, SH110X_WHITE); };

    if (ph < 1900) {
        for (int c = 0; c < 10; c++) {
            if (c != 5) cell(c, 6);                  // bottom row, gap in the middle
            if (c < 4 || c > 6) cell(c, 5);          // row above, gap of three
        }
        cell(0, 4); cell(1, 4); cell(8, 4); cell(9, 4);
        uint8_t rot = ph < 500 ? 0 : ph < 900 ? 1 : 2;
        int py = ph < 1500 ? -2 + (int)(ph * 6 / 1500) : 4;
        uint16_t m = Tetris::shape(2, rot);          // T
        bool flash = ph >= 1500 && (ph / 90) % 2 == 0;
        if (ph >= 1500) {
            for (int c = 0; c < 10; c++) {
                if (flash) { cell(c, 5); cell(c, 6); }
            }
            if (!flash) {
                d.fillRect(X0, Y0 + 5 * C, 30, 2 * C, SH110X_BLACK);
            }
        }
        if (ph < 1500 || flash) {
            for (int rr = 0; rr < 4; rr++) {
                for (int cc = 0; cc < 4; cc++) {
                    if ((m & (0x8000 >> (rr * 4 + cc))) && py + rr >= 0) cell(4 + cc, py + rr);
                }
            }
        }
    } else {
        cell(0, 6); cell(1, 6); cell(8, 6); cell(9, 6);   // what is left
    }

}
