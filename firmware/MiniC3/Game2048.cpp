#include "Game2048.h"

#include <Adafruit_SH110X.h>

static const int GRID_X = 1, GRID_Y = 1, TS = 14, GAP = 2;   // tiles 14x14, grid x 1..62
static const uint32_t SLIDE_MS = 90;
static const uint32_t POP_MS = 120;

static String label(uint16_t v) {
    if (v >= 1024) return String(v / 1024) + "k";
    return String(v);
}

void Game2048::drawTile(Adafruit_GFX& d, int x, int y, int size, uint16_t v) {
    if (size <= 2) return;
    bool filled = v >= 8;
    if (filled) d.fillRoundRect(x, y, size, size, 2, SH110X_WHITE);
    else d.drawRoundRect(x, y, size, size, 2, SH110X_WHITE);
    if (v >= 128 && size >= 10) d.drawRoundRect(x + 2, y + 2, size - 4, size - 4, 1, SH110X_BLACK);
    if (size < 9) return;
    String s = label(v);
    int w = GameUi::tinyWidth(s);
    GameUi::tinyNumber(d, x + (size - w) / 2, y + (size - 5) / 2, s, filled ? SH110X_BLACK : SH110X_WHITE);
}

void Game2048::start(uint32_t now) {
    memset(grid, 0, sizeof(grid));
    score = 0;
    newRecord = false;
    won = false;
    prev = {true, true, true, true, true};
    addTile(grid, now);
    addTile(grid, now);
    phase = P_PLAY;
    phaseAt = now;
}

void Game2048::pause() {
    if (phase != P_PLAY && phase != P_SLIDE) return;
    if (phase == P_SLIDE) memcpy(grid, after, sizeof(grid));
    phase = P_PAUSE;
    pauseSel = 0;
    led.off();
}

void Game2048::addTile(uint16_t (*g)[4], uint32_t now) {
    int empty[16][2], n = 0;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!g[r][c]) {
                empty[n][0] = r;
                empty[n][1] = c;
                n++;
            }
        }
    }
    if (!n) return;
    int k = random(0, n);
    g[empty[k][0]][empty[k][1]] = (random(0, 10) == 0) ? 4 : 2;
    newR = empty[k][0];
    newC = empty[k][1];
    newAt = now;
}

bool Game2048::canMove() const {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (!grid[r][c]) return true;
            if (c < 3 && grid[r][c] == grid[r][c + 1]) return true;
            if (r < 3 && grid[r][c] == grid[r + 1][c]) return true;
        }
    }
    return false;
}

// Slides every line toward `dir`. Fills after[] and the list of moves to
// animate. Returns false if nothing moved.
bool Game2048::slide(int dir, uint32_t now) {
    memset(after, 0, sizeof(after));
    moveCount = 0;
    bool moved = false;
    uint32_t gained = 0;
    uint16_t biggest = 0;

    for (int line = 0; line < 4; line++) {
        // cells of this line, starting from the side the tiles go to
        int cells[4][2];
        for (int i = 0; i < 4; i++) {
            switch (dir) {
                case 0: cells[i][0] = i;     cells[i][1] = line; break;   // up
                case 1: cells[i][0] = 3 - i; cells[i][1] = line; break;   // down
                case 2: cells[i][0] = line;  cells[i][1] = i;     break;  // left
                default: cells[i][0] = line; cells[i][1] = 3 - i; break;  // right
            }
        }
        int out = 0;
        bool canJoin = false;
        for (int i = 0; i < 4; i++) {
            int r = cells[i][0], c = cells[i][1];
            uint16_t v = grid[r][c];
            if (!v) continue;
            int tr, tc;
            if (canJoin && after[cells[out - 1][0]][cells[out - 1][1]] == v) {
                tr = cells[out - 1][0];
                tc = cells[out - 1][1];
                after[tr][tc] = v * 2;
                gained += v * 2;
                biggest = max(biggest, (uint16_t)(v * 2));
                canJoin = false;
            } else {
                tr = cells[out][0];
                tc = cells[out][1];
                after[tr][tc] = v;
                out++;
                canJoin = true;
            }
            if (tr != r || tc != c) moved = true;
            moves[moveCount++] = {(int8_t)r, (int8_t)c, (int8_t)tr, (int8_t)tc, v};
        }
    }
    if (!moved) return false;

    score += gained;
    if (gained) led.set(biggest >= 256 ? 30 : 0, biggest >= 256 ? 26 : 12, biggest >= 256 ? 0 : 25, 60, now);
    if (biggest >= 2048 && !won) {
        won = true;
        toastUntil = now + 2500;
        led.set(30, 26, 0, 600, now);
    }
    return true;
}

void Game2048::finish(uint32_t now) {
    newRecord = score > best;
    if (newRecord) {
        best = score;
        GameUi::saveRecord("g48_hi", best);
    }
    led.set(40, 0, 0, 400, now);
    phase = P_OVER;
    phaseAt = now;
}

void Game2048::frame(Adafruit_GFX& d, const GameKeys& k, uint32_t now) {
    GameKeys e = GameUi::edges(k, prev);
    led.tick(now);

    switch (phase) {
        case P_PAUSE: {
            int r = GameUi::pauseInput(e, pauseSel);
            if (r == 1) phase = P_PLAY;
            else if (r == 2) {
                if (score > best) {
                    best = score;
                    GameUi::saveRecord("g48_hi", best);
                }
                phase = P_OFF;
                return;
            }
            break;
        }
        case P_PLAY: {
            if (e.a) {
                pause();
                break;
            }
            int dir = e.up ? 0 : e.down ? 1 : e.left ? 2 : e.right ? 3 : -1;
            if (dir >= 0 && slide(dir, now)) {
                phase = P_SLIDE;
                phaseAt = now;
                newR = -1;
            }
            break;
        }
        case P_SLIDE:
            if (now - phaseAt >= SLIDE_MS) {
                memcpy(grid, after, sizeof(grid));
                addTile(grid, now);
                phase = P_PLAY;
                if (!canMove()) finish(now);
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

void Game2048::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    // empty slots
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int x = GRID_X + c * (TS + GAP) + TS / 2, y = GRID_Y + r * (TS + GAP) + TS / 2;
            d.drawPixel(x, y, SH110X_WHITE);
        }
    }

    if (phase == P_SLIDE) {
        float t = min(1.0f, (now - phaseAt) / (float)SLIDE_MS);
        for (int i = 0; i < moveCount; i++) {
            const Move& m = moves[i];
            float x = GRID_X + (m.fc + (m.tc - m.fc) * t) * (TS + GAP);
            float y = GRID_Y + (m.fr + (m.tr - m.fr) * t) * (TS + GAP);
            drawTile(d, (int)x, (int)y, TS, m.v);
        }
    } else {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (!grid[r][c]) continue;
                int size = TS;
                if (r == newR && c == newC && now - newAt < POP_MS) size = 4 + (TS - 4) * (now - newAt) / POP_MS;
                int x = GRID_X + c * (TS + GAP) + (TS - size) / 2, y = GRID_Y + r * (TS + GAP) + (TS - size) / 2;
                drawTile(d, x, y, size, grid[r][c]);
            }
        }
    }

    // right side
    d.setCursor(70, 2);
    d.print("PUNTOS");
    d.setCursor(70, 12);
    d.print(score);
    d.setCursor(70, 28);
    d.print("RECORD");
    d.setCursor(70, 38);
    d.print(max(best, score));
    d.setCursor(70, 55);
    d.print("A pausa");

    if (toastUntil && (int32_t)(toastUntil - now) > 0) {
        d.fillRect(4, 22, 120, 20, SH110X_BLACK);
        d.drawRect(4, 22, 120, 20, SH110X_WHITE);
        GameUi::centerText(d, "LLEGASTE A 2048!", 25);
        GameUi::centerText(d, "podes seguir", 33);
    }
    if (phase == P_PAUSE) GameUi::drawPause(d, pauseSel);
    if (phase == P_OVER) GameUi::drawOver(d, "SIN MOVIDAS", String(score), "Record " + String(best), newRecord, now, phaseAt);
}

void Game2048::drawDemo(Adafruit_GFX& d, uint32_t now, int ox) {
    // two tiles slide together and become one, over and over
    uint32_t p = now % 2000;
    int y = 34;
    if (p < 700) {
        drawTile(d, 30 + ox, y, 14, 2);
        drawTile(d, 84 + ox, y, 14, 2);
    } else if (p < 900) {
        float t = (p - 700) / 200.0f;
        drawTile(d, 30 + (int)(27 * t) + ox, y, 14, 2);
        drawTile(d, 84 - (int)(27 * t) + ox, y, 14, 2);
    } else {
        int size = p < 1050 ? 14 + (int)(4 * (1 - (p - 900) / 150.0f)) : 14;
        drawTile(d, 57 - (size - 14) / 2 + ox, y - (size - 14) / 2, size, 4);
    }
    drawTile(d, 10 + ox, y, 14, 8);
    drawTile(d, 104 + ox, y, 14, 64);
}
