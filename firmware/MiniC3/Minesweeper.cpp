#include "Minesweeper.h"

#include <Adafruit_SH110X.h>

static const int GX = 0, GY = 8, CS = 8;   // cells 8x8, grid y 8..63
static const uint32_t FLAG_MS = 450;
static const uint32_t PAUSE_MS = 1500;

void Minesweeper::start(uint32_t now) {
    memset(cell, 0, sizeof(cell));
    placed = false;
    cx = COLS / 2;
    cy = ROWS / 2;
    opened = flags = 0;
    elapsed = 0;
    won = false;
    newRecord = false;
    boomX = boomY = -1;
    prev = {true, true, true, true, true};
    aStage = 2;   // A must be released first
    repDir = -1;
    phase = P_PLAY;
    phaseAt = now;
    startAt = now;
}

void Minesweeper::pause() {
    if (phase != P_PLAY) return;
    elapsed = seconds(millis());
    phase = P_PAUSE;
    pauseSel = 0;
    led.off();
}

uint32_t Minesweeper::seconds(uint32_t now) const {
    if (phase != P_PLAY || !placed) return elapsed;
    return min((uint32_t)999, elapsed + (now - startAt) / 1000);
}

void Minesweeper::placeMines(int sx, int sy) {
    int n = 0;
    while (n < MINES) {
        int x = random(0, COLS), y = random(0, ROWS);
        if (abs(x - sx) <= 1 && abs(y - sy) <= 1) continue;   // the first cell and around it are free
        if (cell[y][x] & MINE) continue;
        cell[y][x] |= MINE;
        n++;
    }
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            int c = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < COLS && ny >= 0 && ny < ROWS && (cell[ny][nx] & MINE)) c++;
                }
            }
            cell[y][x] = (cell[y][x] & MINE) | c;
        }
    }
    placed = true;
}

void Minesweeper::lose(int x, int y, uint32_t now) {
    elapsed = seconds(now);
    boomX = x;
    boomY = y;
    led.set(45, 0, 0, 600, now);
    phase = P_BOOM;
    phaseAt = now;
}

void Minesweeper::open(int x, int y, uint32_t now) {
    // flood fill without recursion (small stack)
    static int8_t stack[COLS * ROWS][2];
    int sp = 0;
    stack[sp][0] = x;
    stack[sp][1] = y;
    sp++;
    while (sp > 0) {
        sp--;
        int px = stack[sp][0], py = stack[sp][1];
        uint8_t& c = cell[py][px];
        if (c & (OPEN | FLAG)) continue;
        if (c & MINE) {
            lose(px, py, now);
            return;
        }
        c |= OPEN;
        opened++;
        if ((c & 0x0F) == 0) {
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = px + dx, ny = py + dy;
                    if (nx >= 0 && nx < COLS && ny >= 0 && ny < ROWS && !(cell[ny][nx] & (OPEN | FLAG)) && sp < COLS * ROWS) {
                        stack[sp][0] = nx;
                        stack[sp][1] = ny;
                        sp++;
                    }
                }
            }
        }
    }
}

void Minesweeper::chord(int x, int y, uint32_t now) {
    int need = cell[y][x] & 0x0F, f = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < COLS && ny >= 0 && ny < ROWS && (cell[ny][nx] & FLAG)) f++;
        }
    }
    if (f != need || need == 0) return;
    for (int dy = -1; dy <= 1 && phase == P_PLAY; dy++) {
        for (int dx = -1; dx <= 1 && phase == P_PLAY; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < COLS && ny >= 0 && ny < ROWS) open(nx, ny, now);
        }
    }
}

void Minesweeper::checkWin(uint32_t now) {
    if (phase != P_PLAY || opened < COLS * ROWS - MINES) return;
    elapsed = seconds(now);
    won = true;
    newRecord = (best == 0 || elapsed < best);
    if (newRecord) {
        best = max((uint32_t)1, elapsed);
        GameUi::saveRecord("min_hi", best);
    }
    led.set(0, 35, 0, 600, now);
    phase = P_OVER;
    phaseAt = now;
}

void Minesweeper::frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) {
    bool aWas = prev.a;
    GameKeys e = GameUi::edges(k, prev);
    led.tick(now);

    switch (phase) {
        case P_PAUSE: {
            int r = GameUi::pauseInput(e, pauseSel);
            if (r == 1) {
                phase = P_PLAY;
                startAt = now;
                aStage = 2;
            } else if (r == 2) {
                phase = P_OFF;
                return;
            }
            break;
        }
        case P_PLAY: {
            // move, repeating while held
            int dir = k.up ? 0 : k.down ? 1 : k.left ? 2 : k.right ? 3 : -1;
            bool fresh = e.up || e.down || e.left || e.right;
            bool go = false;
            if (fresh) {
                repDir = dir;
                repAt = now + 260;
                go = true;
            } else if (dir >= 0 && dir == repDir && (int32_t)(now - repAt) >= 0) {
                repAt = now + 90;
                go = true;
            } else if (dir < 0) {
                repDir = -1;
            }
            if (go && dir >= 0) {
                if (dir == 0) cy = (cy + ROWS - 1) % ROWS;
                if (dir == 1) cy = (cy + 1) % ROWS;
                if (dir == 2) cx = (cx + COLS - 1) % COLS;
                if (dir == 3) cx = (cx + 1) % COLS;
            }

            // A: tap opens, held = flag, held longer = pause
            if (e.a) {
                aSince = now;
                aStage = 0;
            }
            if (k.a && aStage == 0 && now - aSince >= FLAG_MS) {
                aStage = 1;
                uint8_t& c = cell[cy][cx];
                if (!(c & OPEN)) {
                    c ^= FLAG;
                    flags += (c & FLAG) ? 1 : -1;
                    led.set(0, 0, 25, 60, now);
                }
            }
            if (k.a && aStage == 1 && now - aSince >= PAUSE_MS) {
                // it was a pause, not a flag: undo the flag
                uint8_t& c = cell[cy][cx];
                if (!(c & OPEN)) {
                    c ^= FLAG;
                    flags += (c & FLAG) ? 1 : -1;
                }
                aStage = 2;
                pause();
                break;
            }
            if (!k.a && aWas && aStage == 0 && now - aSince < FLAG_MS) {
                // released quickly: open
                aStage = 2;
                uint8_t c = cell[cy][cx];
                if (!placed) {
                    placeMines(cx, cy);
                    startAt = now;
                }
                if (c & OPEN) chord(cx, cy, now);
                else open(cx, cy, now);
                checkWin(now);
            }
            break;
        }
        case P_BOOM:
            if (now - phaseAt > 1500) {
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

static void drawMine(Adafruit_GFX& d, int x, int y, uint16_t col) {
    d.fillCircle(x + 3, y + 3, 2, col);
    d.drawPixel(x + 3, y, col);
    d.drawPixel(x + 3, y + 6, col);
    d.drawPixel(x, y + 3, col);
    d.drawPixel(x + 6, y + 3, col);
}

static void drawFlag(Adafruit_GFX& d, int x, int y, uint16_t col) {
    d.drawFastVLine(x + 2, y + 1, 5, col);
    d.fillTriangle(x + 3, y + 1, x + 3, y + 4, x + 5, y + 2, col);
    d.drawFastHLine(x + 1, y + 6, 4, col);
}

void Minesweeper::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    // top: mines left and time
    drawMine(d, 0, 0, SH110X_WHITE);
    d.setCursor(9, 0);
    d.print(MINES - flags);
    String t = String(seconds(now)) + " s";
    d.setCursor(128 - (int)t.length() * 6 + 1, 0);
    d.print(t);

    bool showAll = (phase == P_BOOM || (phase == P_OVER && !won));
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            uint8_t c = cell[y][x];
            int px = GX + x * CS, py = GY + y * CS;
            if (c & OPEN) {
                int n = c & 0x0F;
                if (n) GameUi::tinyNumber(d, px + 2, py + 1, String(n), SH110X_WHITE);
            } else if (showAll && (c & MINE)) {
                if (x == boomX && y == boomY) {
                    d.fillRect(px, py, CS - 1, CS - 1, SH110X_WHITE);
                    drawMine(d, px, py, SH110X_BLACK);
                } else {
                    drawMine(d, px, py, SH110X_WHITE);
                }
            } else {
                d.fillRect(px, py, CS - 1, CS - 1, SH110X_WHITE);
                if (c & FLAG) drawFlag(d, px, py, SH110X_BLACK);
            }
        }
    }

    // cursor (blinks)
    if (phase == P_PLAY && (now / 300) % 3 != 0) {
        int px = GX + cx * CS, py = GY + cy * CS;
        bool openCell = cell[cy][cx] & OPEN;
        d.drawRect(px - 1, py - 1, CS + 1, CS + 1, openCell ? SH110X_WHITE : SH110X_BLACK);
        if (!openCell) d.drawRect(px + 1, py + 1, CS - 3, CS - 3, SH110X_BLACK);
    }

    if (phase == P_PAUSE) GameUi::drawPause(d, pauseSel);
    if (phase == P_OVER) {
        GameUi::drawOver(d, won ? "GANASTE!" : "BOOM!", String(elapsed) + " s",
                         best ? "Mejor " + String(best) + " s" : String("Sin record"), won && newRecord, now, phaseAt);
    }
}

void Minesweeper::drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {
    // a little board that opens bit by bit, a flag and a mine
    static const char* B[3] = {"1F2#", "112#", "0112"};
    int open = (now / 400) % 7;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 8; c++) {
            int px = 32 + c * 8 + ox, py = 30 + r * 8;
            int col = c % 4;
            char ch = B[r][col];
            bool isOpen = (c < 4) ? (r * 4 + col) < open * 2 : false;
            if (isOpen && ch != '#' && ch != 'F') {
                if (ch != '0') GameUi::tinyNumber(d, px + 2, py + 1, String(ch), SH110X_WHITE);
            } else {
                d.fillRect(px, py, 7, 7, SH110X_WHITE);
                if (ch == 'F' && c < 4) drawFlag(d, px, py, SH110X_BLACK);
            }
        }
    }
}
