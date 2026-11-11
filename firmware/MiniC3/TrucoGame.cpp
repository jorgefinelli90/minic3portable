#include "TrucoGame.h"

#include <Adafruit_SH110X.h>
#include <Preferences.h>

#include "MiniC3.h"

typedef TrucoEngine TE;
static const uint8_t HUMAN = TE::HUMAN;
static const uint8_t CPU = TE::CPU;

static const int CW = 16, CH = 19;           // card size
static const uint32_t FLY_MS = 220;
static const uint32_t TRICK_HOLD_MS = 1000;
static const int HAND_X[3] = {44, 64, 84};
static const int HAND_Y = 45;

// 8x8 suits (bit 7 = left pixel) and 5x5 versions (bit 4 = left pixel)
static const uint8_t SUIT8[4][8] = {
    {0x10, 0x38, 0x38, 0x38, 0x38, 0xFE, 0x38, 0x10},   // espada: blade, guard, handle
    {0x38, 0x7C, 0x7C, 0x38, 0x38, 0x10, 0x10, 0x38},   // basto: a club
    {0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C},   // oro
    {0xFF, 0x81, 0x42, 0x3C, 0x18, 0x18, 0x3C, 0x7E},   // copa
};
static const uint8_t SUIT5[4][5] = {
    {0x04, 0x04, 0x04, 0x0E, 0x04},
    {0x0E, 0x0E, 0x04, 0x04, 0x04},
    {0x0E, 0x11, 0x15, 0x11, 0x0E},
    {0x1F, 0x11, 0x0E, 0x04, 0x0E},
};

// ---------------------------------------------------------------------------
//  Setup / stats
// ---------------------------------------------------------------------------

void TrucoGame::begin() {
    Preferences p;
    p.begin("minic3_game", true);
    wins = p.getUShort("tr_w", 0);
    losses = p.getUShort("tr_l", 0);
    ai.level = p.getUChar("tr_lv", 1) ? 1 : 0;
    p.end();
}

void TrucoGame::saveStats() {
    Preferences p;
    p.begin("minic3_game", false);
    p.putUShort("tr_w", wins);
    p.putUShort("tr_l", losses);
    p.putUChar("tr_lv", ai.level);
    p.end();
}

void TrucoGame::toggleLevel() {
    ai.level = ai.level ? 0 : 1;
    saveStats();
}

void TrucoGame::start(uint32_t now) {
    g.newGame(random(0, 2));
    g.eventCount = 0;
    mode = M_PLAY;
    modeAt = now;
    queueN = 0;
    curActive = false;
    flyP = -1;
    holdTrick = -1;
    holdUntil = 0;
    cpuAt = 0;
    statsSaved = false;
    confirmQuit = false;
    lastCall = -1;
    winPulseAt = 0;
    sel = 0;
    fixSel();
    char sub[22];
    snprintf(sub, sizeof(sub), "Es mano %s", g.mano == HUMAN ? "vos" : "la CPU");
    addBanner("", "A 15", sub, 1300);
}

// ---------------------------------------------------------------------------
//  LED (the C3-Zero LED takes RGB; rgbLedWrite sends GRB)
// ---------------------------------------------------------------------------

void TrucoGame::led(uint8_t r, uint8_t gr, uint8_t b, uint16_t ms, uint32_t now) {
#if MINI_LED_RGB_ORDER
    rgbLedWrite(MINI_RGB_LED_PIN, gr, r, b);
#else
    rgbLedWrite(MINI_RGB_LED_PIN, r, gr, b);
#endif
    ledOn = true;
    ledUntil = now + ms;
}

void TrucoGame::ledOff() {
    if (!ledOn) return;
    rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);
    ledOn = false;
}

// ---------------------------------------------------------------------------
//  Messages from the game
// ---------------------------------------------------------------------------

void TrucoGame::addBanner(const char* who, const char* text, const char* sub, uint16_t ms) {
    if (queueN >= sizeof(queue) / sizeof(queue[0])) return;
    Banner& b = queue[queueN++];
    strncpy(b.who, who, sizeof(b.who) - 1);
    b.who[sizeof(b.who) - 1] = 0;
    strncpy(b.text, text, sizeof(b.text) - 1);
    b.text[sizeof(b.text) - 1] = 0;
    strncpy(b.sub, sub, sizeof(b.sub) - 1);
    b.sub[sizeof(b.sub) - 1] = 0;
    b.ms = ms;
}

const char* TrucoGame::callText(uint8_t act, int level) {
    switch (act) {
        case TE::A_ENVIDO: return "ENVIDO!";
        case TE::A_REAL: return "REAL ENVIDO!";
        case TE::A_FALTA: return "FALTA ENVIDO!";
        case TE::A_TRUCO: return level == 2 ? "TRUCO!" : level == 3 ? "RETRUCO!" : "VALE CUATRO!";
        default: return "";
    }
}

void TrucoGame::consumeEvents(uint32_t now) {
    for (int i = 0; i < g.eventCount; i++) {
        const TE::Event& e = g.events[i];
        const char* who = (e.p == CPU) ? "CPU" : "VOS";
        const char* other = (e.p == CPU) ? "vos" : "la CPU";
        char sub[22];

        switch (e.type) {
            case TE::EV_CARD:
                flyP = e.p;
                flyIdx = e.a;
                flyTrick = e.b;
                flyAt = now;
                break;

            case TE::EV_TRICK:
                holdTrick = e.a;
                holdUntil = 1;   // starts when the card lands
                break;

            case TE::EV_CALL:
                if (e.p == CPU) {
                    lastCall = e.a;
                    lastCallLevel = e.b;
                    led(30, 10, 0, 300, now);
                }
                addBanner(who, callText(e.a, e.b), "", e.p == CPU ? 1400 : 800);
                break;

            case TE::EV_QUIERO:
                addBanner(who, "QUIERO!", "", e.p == CPU ? 1100 : 600);
                break;

            case TE::EV_NOQUIERO:
                snprintf(sub, sizeof(sub), "+%d para %s", e.b, other);
                addBanner(who, "NO QUIERO", sub, 1500);
                break;

            case TE::EV_ENVIDO_RESULT: {
                char text[18];
                snprintf(text, sizeof(text), "VOS %d - CPU %d", e.a, e.b);
                snprintf(sub, sizeof(sub), "+%d para %s", e.c, e.p == HUMAN ? "vos" : "la CPU");
                addBanner("Tantos", text, sub, 2600);
                break;
            }

            case TE::EV_MAZO:
                snprintf(sub, sizeof(sub), "+%d para %s", e.b, other);
                addBanner(who, "AL MAZO", sub, 1400);
                break;

            case TE::EV_HAND_END:
                if (e.p == HUMAN) led(0, 30, 0, 350, now);
                else led(35, 0, 0, 350, now);
                break;

            case TE::EV_GAME_END:
                if (e.p == HUMAN) winPulseAt = now;
                break;
        }
    }
    g.eventCount = 0;
}

// ---------------------------------------------------------------------------
//  Keys
// ---------------------------------------------------------------------------

bool TrucoGame::idle(uint32_t now) const {
    if (curActive || queueN > 0) return false;
    if (flyP >= 0 || holdTrick >= 0) return false;
    return true;
}

void TrucoGame::fixSel() {
    for (int k = 0; k < 3; k++) {
        if (!g.played[HUMAN][(sel + k) % 3]) {
            sel = (sel + k) % 3;
            return;
        }
    }
}

void TrucoGame::humanAct(TE::Act a, uint32_t now) {
    if (!g.apply(HUMAN, a)) return;
    mode = M_PLAY;
    confirmQuit = false;
    consumeEvents(now);
    fixSel();
}

const char* TrucoGame::itemLabel(uint8_t it) const {
    switch (it) {
        case TE::A_QUIERO: return "Quiero";
        case TE::A_NOQUIERO: return "No quiero";
        case TE::A_ENVIDO: return "Envido";
        case TE::A_REAL: return "Real env.";
        case TE::A_FALTA: return "Falta env.";
        case TE::A_TRUCO: {
            uint8_t next = (g.phase == TE::PH_RESP_TRUCO) ? g.trucoPending + 1 : g.trucoLevel + 1;
            return next == 2 ? "Truco" : next == 3 ? "Retruco" : "Vale 4";
        }
        case TE::A_MAZO: return "Al mazo";
        case IT_QUIT: return confirmQuit ? "A: salir?" : "Abandonar";
        default: return "";
    }
}

void TrucoGame::openMenu(bool answering) {
    respond = answering;
    itemCount = 0;
    menuSel = 0;
    confirmQuit = false;
    static const uint8_t SING[] = {TE::A_TRUCO, TE::A_ENVIDO, TE::A_REAL, TE::A_FALTA, TE::A_MAZO};
    static const uint8_t ANSWER[] = {TE::A_QUIERO, TE::A_NOQUIERO, TE::A_TRUCO, TE::A_ENVIDO, TE::A_REAL, TE::A_FALTA};
    const uint8_t* list = answering ? ANSWER : SING;
    int n = answering ? sizeof(ANSWER) : sizeof(SING);
    for (int i = 0; i < n; i++) {
        if (g.legal(HUMAN, (TE::Act)list[i])) items[itemCount++] = list[i];
    }
    if (!answering) items[itemCount++] = IT_QUIT;

    if (answering) {
        snprintf(menuTitle, sizeof(menuTitle), "CPU: %s", lastCall >= 0 ? callText(lastCall, lastCallLevel) : "");
        char* bang = strchr(menuTitle, '!');
        if (bang) *bang = 0;
    } else {
        strcpy(menuTitle, "Cantar");
    }
    mode = M_MENU;
}

void TrucoGame::chooseItem(uint32_t now) {
    if (menuSel < 0 || menuSel >= itemCount) return;
    uint8_t it = items[menuSel];
    if (it == IT_QUIT) {
        if (!confirmQuit) {
            confirmQuit = true;
            return;
        }
        ledOff();
        mode = M_OFF;   // abandoned: not counted
        return;
    }
    humanAct((TE::Act)it, now);
}

void TrucoGame::onEvent(InputEvent e) {
    uint32_t now = millis();
    if (!idle(now)) return;

    if (mode == M_MENU) {
        int n = itemCount;
        switch (e) {
            case INPUT_LEFT:  menuSel = (menuSel + n - 1) % n; break;
            case INPUT_RIGHT: menuSel = (menuSel + 1) % n; break;
            case INPUT_DOWN:  if (menuSel + 2 < n) menuSel += 2; else menuSel = n - 1; break;
            case INPUT_UP:
                if (menuSel >= 2) menuSel -= 2;
                else if (!respond) mode = M_PLAY;   // close
                break;
            default: break;
        }
        if (items[menuSel] != IT_QUIT) confirmQuit = false;
        return;
    }

    if (mode == M_PLAY && g.actor() == HUMAN && g.phase == TE::PH_PLAY) {
        switch (e) {
            case INPUT_LEFT:
                for (int k = 1; k <= 3; k++) {
                    int s = (sel + 3 - k) % 3;
                    if (!g.played[HUMAN][s]) { sel = s; break; }
                }
                break;
            case INPUT_RIGHT:
                for (int k = 1; k <= 3; k++) {
                    int s = (sel + k) % 3;
                    if (!g.played[HUMAN][s]) { sel = s; break; }
                }
                break;
            case INPUT_DOWN:
                openMenu(false);
                break;
            default:
                break;
        }
    }
}

void TrucoGame::onA(uint32_t now) {
    if (mode == M_OFF) return;

    // A during a message: skip it
    if (curActive) {
        curUntil = now;
        return;
    }
    if (!idle(now)) return;

    switch (mode) {
        case M_MENU:
            chooseItem(now);
            break;
        case M_PLAY:
            if (g.actor() == HUMAN && g.phase == TE::PH_PLAY && !g.played[HUMAN][sel]) {
                humanAct((TE::Act)(TE::A_PLAY0 + sel), now);
            }
            break;
        case M_HAND_END:
            if (now - modeAt > 400) {
                g.newHand();
                mode = M_PLAY;
                sel = 0;
                fixSel();
                cpuAt = 0;
                lastCall = -1;
            }
            break;
        case M_GAME_END:
            if (now - modeAt > 1200) {
                ledOff();
                mode = M_OFF;
            }
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
//  Game flow
// ---------------------------------------------------------------------------

void TrucoGame::frame(Adafruit_GFX& d, uint32_t now) {
    if (mode == M_OFF) return;

    // LED
    if (winPulseAt && now - winPulseAt < 2400) {
        bool want = ((now - winPulseAt) / 200) % 2 == 0;
        if (want && !ledOn) led(30, 26, 0, 3000, now);
        else if (!want && ledOn) ledOff();
    } else if (ledOn && (int32_t)(now - ledUntil) >= 0) {
        ledOff();
    }

    // things on screen that have to finish first
    if (curActive && (int32_t)(now - curUntil) >= 0) curActive = false;
    if (flyP >= 0 && now - flyAt >= FLY_MS) {
        flyP = -1;
        if (holdUntil == 1) holdUntil = now + TRICK_HOLD_MS;
    }
    if (flyP < 0 && holdTrick >= 0 && holdUntil > 1 && (int32_t)(now - holdUntil) >= 0) {
        holdTrick = -1;
        holdUntil = 0;
    }
    if (!curActive && flyP < 0 && holdTrick < 0 && queueN > 0) {
        cur = queue[0];
        for (int i = 1; i < queueN; i++) queue[i - 1] = queue[i];
        queueN--;
        curActive = true;
        curUntil = now + cur.ms;
    }

    if (idle(now)) {
        if (g.phase == TE::PH_GAME_OVER) {
            if (mode != M_GAME_END) {
                mode = M_GAME_END;
                modeAt = now;
                if (!statsSaved) {
                    statsSaved = true;
                    if (g.score[HUMAN] >= g.target) wins++;
                    else losses++;
                    saveStats();
                }
            }
        } else if (g.phase == TE::PH_HAND_OVER) {
            if (mode != M_HAND_END) {
                mode = M_HAND_END;
                modeAt = now;
            }
        } else {
            int who = g.actor();
            if (who == CPU) {
                if (mode == M_MENU) mode = M_PLAY;
                if (!cpuAt) cpuAt = now + (g.phase == TE::PH_PLAY ? random(650, 1150) : random(800, 1300));
                if ((int32_t)(now - cpuAt) >= 0) {
                    cpuAt = 0;
                    TE::Act a = ai.decide(g, CPU);
                    if (!g.apply(CPU, a)) {
                        // never expected: play any card / answer something legal
                        for (int k = 0; k < TE::A_COUNT; k++) {
                            if (g.apply(CPU, (TE::Act)k)) break;
                        }
                    }
                    consumeEvents(now);
                }
            } else if (who == HUMAN) {
                cpuAt = 0;
                bool answering = (g.phase != TE::PH_PLAY);
                if (answering && !(mode == M_MENU && respond)) openMenu(true);
                if (!answering && mode == M_MENU && respond) mode = M_PLAY;
            }
        }
    }

    draw(d, now);
}

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------

void TrucoGame::drawSuit(Adafruit_GFX& d, int x, int y, uint8_t suit, bool big, uint16_t color) {
    if (big) {
        for (int r = 0; r < 8; r++) {
            for (int b = 0; b < 8; b++) {
                if (SUIT8[suit][r] & (0x80 >> b)) d.drawPixel(x + b, y + r, color);
            }
        }
    } else {
        for (int r = 0; r < 5; r++) {
            for (int b = 0; b < 5; b++) {
                if (SUIT5[suit][r] & (0x10 >> b)) d.drawPixel(x + b, y + r, color);
            }
        }
    }
}

void TrucoGame::drawCard(Adafruit_GFX& d, int x, int y, TCard c) {
    d.fillRoundRect(x, y, CW, CH, 2, SH110X_BLACK);
    d.drawRoundRect(x, y, CW, CH, 2, SH110X_WHITE);
    d.setTextSize(1);
    d.setTextColor(SH110X_WHITE);
    d.setCursor(x + 2, y + 2);
    d.print(c.num);
    drawSuit(d, x + 4, y + 10, c.suit, true, SH110X_WHITE);
}

void TrucoGame::drawBack(Adafruit_GFX& d, int x, int y, int w, int h) {
    d.fillRoundRect(x, y, w, h, 2, SH110X_BLACK);
    d.drawRoundRect(x, y, w, h, 2, SH110X_WHITE);
    for (int yy = y + 2; yy < y + h - 2; yy++) {
        for (int xx = x + 2; xx < x + w - 2; xx++) {
            if (((xx + yy) & 1) == 0) d.drawPixel(xx, yy, SH110X_WHITE);
        }
    }
}

void TrucoGame::drawMicro(Adafruit_GFX& d, int x, int y, TCard c, bool invert) {
    uint16_t fg = invert ? SH110X_BLACK : SH110X_WHITE;
    int w = (c.num >= 10 ? 11 : 5) + 6;
    if (invert) d.fillRect(x - 1, y - 1, w + 1, 9, SH110X_WHITE);
    d.setTextColor(fg);
    d.setCursor(x, y);
    d.print(c.num);
    drawSuit(d, x + w - 5, y + 1, c.suit, false, fg);
    d.setTextColor(SH110X_WHITE);
}

void TrucoGame::slotPos(uint8_t p, int& x, int& y) const {
    if (p == CPU) {
        x = 44;
        y = 11;
    } else {
        x = 64;
        y = 22;
    }
}

void TrucoGame::draw(Adafruit_GFX& d, uint32_t now) {
    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    // --- score bar ---
    for (int p = 0; p < 2; p++) {
        int x = (p == HUMAN) ? 0 : 42;
        if (g.mano == p) d.fillRect(x, 2, 3, 3, SH110X_WHITE);
        d.setCursor(x + 5, 0);
        d.print(p == HUMAN ? "Vos " : "CPU ");
        d.print(min(g.score[p], g.target));
    }
    const char* tl = g.trucoLevel == 2 ? "TRUCO" : g.trucoLevel == 3 ? "RETRUCO" : g.trucoLevel == 4 ? "VALE 4" : "a 15";
    d.setCursor(127 - (int)strlen(tl) * 6 + 1, 0);
    d.print(tl);
    d.drawFastHLine(0, 8, 128, SH110X_WHITE);

    if (mode == M_GAME_END) {
        bool won = g.score[HUMAN] >= g.target;
        const char* t = won ? "GANASTE!" : "PERDISTE";
        d.setTextSize(2);
        d.setCursor((128 - (int)strlen(t) * 12 + 2) / 2, 12);
        d.print(t);
        char sc[12];
        snprintf(sc, sizeof(sc), "%d a %d", min(g.score[HUMAN], g.target), min(g.score[CPU], g.target));
        d.setCursor((128 - (int)strlen(sc) * 12 + 2) / 2, 30);
        d.print(sc);
        d.setTextSize(1);
        char st[24];
        snprintf(st, sizeof(st), "Ganadas %u  Perdidas %u", wins, losses);
        d.setCursor((128 - (int)strlen(st) * 6 + 1) / 2, 47);
        d.print(st);
        if (now - modeAt > 1200 && (now / 500) % 2 == 0) {
            d.setCursor(37, 56);
            d.print("A: volver");
        }
        return;
    }

    // --- tricks already played (left): CPU card, your card; the winner inverted ---
    for (int t = 0; t < 3; t++) {
        if (g.trickWin[t] < 0 || t == holdTrick) continue;
        int y = 11 + t * 11;
        TCard cc = g.hand[CPU][g.table[t][CPU]];
        TCard hc = g.hand[HUMAN][g.table[t][HUMAN]];
        drawMicro(d, 1, y, cc, g.trickWin[t] == CPU);
        drawMicro(d, 21, y, hc, g.trickWin[t] == HUMAN);
        if (g.trickWin[t] == 2) d.drawFastHLine(18, y + 3, 2, SH110X_WHITE);
    }

    // --- table: the trick being played (or the one that just finished) ---
    int tt = (holdTrick >= 0) ? holdTrick : g.trick;
    for (int p = 0; p < 2; p++) {
        int x, y;
        slotPos(p, x, y);
        int8_t idx = g.table[tt][p];
        bool flying = (flyP == p && flyTrick == tt);
        if (idx >= 0 && !flying) {
            drawCard(d, x, y, g.hand[p][idx]);
            if (holdTrick >= 0 && g.trickWin[tt] == p) d.drawRoundRect(x - 2, y - 2, CW + 4, CH + 4, 3, SH110X_WHITE);
        } else if (idx < 0 && g.phase <= TE::PH_RESP_TRUCO && g.turn == p && holdTrick < 0) {
            // whose turn: a dotted outline
            for (int k = 0; k < CW; k += 2) {
                d.drawPixel(x + k, y, SH110X_WHITE);
                d.drawPixel(x + k, y + CH - 1, SH110X_WHITE);
            }
            for (int k = 0; k < CH; k += 2) {
                d.drawPixel(x, y + k, SH110X_WHITE);
                d.drawPixel(x + CW - 1, y + k, SH110X_WHITE);
            }
        }
    }

    // --- the computer's cards (backs) ---
    int left = 0;
    for (int i = 0; i < 3; i++) {
        if (!g.played[CPU][i] || (flyP == CPU && flyIdx == i)) left++;
    }
    if (mode != M_HAND_END) {
        for (int i = 0; i < left; i++) drawBack(d, 96 + i * 8, 11, 11, 15);
    }

    // --- flying card ---
    if (flyP >= 0) {
        float k = min(1.0f, (now - flyAt) / (float)FLY_MS);
        k = 1 - (1 - k) * (1 - k);
        int sx, sy, ex, ey;
        if (flyP == CPU) {
            sx = 96 + (left - 1) * 8;
            sy = 11;
        } else {
            sx = HAND_X[flyIdx];
            sy = HAND_Y;
        }
        slotPos(flyP, ex, ey);
        drawCard(d, sx + (int)((ex - sx) * k), sy + (int)((ey - sy) * k), g.hand[flyP][flyIdx]);
    }

    // --- your hand ---
    bool yourTurn = (mode == M_PLAY && g.phase == TE::PH_PLAY && g.actor() == HUMAN && idle(now));
    for (int i = 0; i < 3; i++) {
        if (g.played[HUMAN][i]) continue;
        int y = HAND_Y;
        if (yourTurn && i == sel) y -= 3;
        drawCard(d, HAND_X[i], y, g.hand[HUMAN][i]);
    }

    // --- help (bottom left) ---
    if (yourTurn) {
        d.setCursor(0, 47);
        d.print("A tira");
        d.setCursor(0, 56);
        d.print("\x19 canta");
    } else if (g.actor() == CPU && idle(now) && mode == M_PLAY) {
        d.setCursor(0, 52);
        d.print("Piensa");
        int dots = (now / 300) % 4;
        for (int i = 0; i < dots; i++) d.fillRect(37 + i * 3, 58, 1, 1, SH110X_WHITE);
    }
    // your envido, while it can still be sung
    if (!g.envDone && g.trick == 0 && g.table[0][HUMAN] < 0 && g.trucoLevel == 1 && mode != M_HAND_END) {
        uint8_t e = envidoOf(g.hand[HUMAN], 3);
        d.setCursor(104, 48);
        d.print("tant");
        d.setCursor(e < 10 ? 113 : 110, 57);
        d.print(e);
    }

    // --- menu ---
    if (mode == M_MENU) {
        d.fillRect(0, 9, 128, 38, SH110X_BLACK);
        d.drawRect(0, 9, 128, 38, SH110X_WHITE);
        d.fillRect(0, 9, 128, 9, SH110X_WHITE);
        d.setTextColor(SH110X_BLACK);
        d.setCursor(3, 10);
        d.print(menuTitle);
        d.setTextColor(SH110X_WHITE);
        int firstRow = max(0, menuSel / 2 - 2);
        for (int i = firstRow * 2; i < itemCount && i < firstRow * 2 + 6; i++) {
            int col = i % 2;
            int row = i / 2 - firstRow;
            int x = 2 + col * 63;
            int y = 20 + row * 9;
            if (i == menuSel) {
                bool blink = confirmQuit && (now / 250) % 2;
                d.fillRect(x - 1, y - 1, 62, 8, blink ? SH110X_BLACK : SH110X_WHITE);
                d.setTextColor(blink ? SH110X_WHITE : SH110X_BLACK);
            }
            d.setCursor(x + 1, y);
            d.print(itemLabel(items[i]));
            d.setTextColor(SH110X_WHITE);
        }
    }

    // --- end of the hand: what the computer had ---
    if (mode == M_HAND_END) {
        d.fillRect(0, 9, 128, 35, SH110X_BLACK);
        d.drawRect(0, 9, 128, 35, SH110X_WHITE);
        char l1[24];
        snprintf(l1, sizeof(l1), "%s +%d", g.handWinner == HUMAN ? "Mano para vos" : "Mano para la CPU", g.handPoints);
        d.setCursor(3, 11);
        d.print(l1);
        d.setCursor(3, 22);
        d.print("CPU tenia:");
        for (int i = 0; i < 3; i++) drawMicro(d, 64 + i * 21, 22, g.hand[CPU][i], false);
        if (g.envShown[CPU] >= 0) {
            d.setCursor(3, 33);
            d.print("Tantos " + String(g.envShown[CPU]));
        }
        if (now - modeAt > 400 && (now / 500) % 2 == 0) {
            d.setCursor(71, 33);
            d.print("A: sigue");
        }
    }

    // --- message ---
    if (curActive) {
        d.fillRoundRect(6, 13, 116, 32, 3, SH110X_BLACK);
        d.drawRoundRect(6, 13, 116, 32, 3, SH110X_WHITE);
        int ty = 19;
        if (cur.who[0]) {
            int ww = strlen(cur.who) * 6 + 3;
            d.fillRect(6, 13, ww, 9, SH110X_WHITE);
            d.setTextColor(SH110X_BLACK);
            d.setCursor(8, 14);
            d.print(cur.who);
            d.setTextColor(SH110X_WHITE);
            ty = 23;
        }
        int len = strlen(cur.text);
        bool big = len <= 9;
        d.setTextSize(big ? 2 : 1);
        int tw = len * (big ? 12 : 6) - (big ? 2 : 1);
        int yy = cur.sub[0] ? (big ? ty - 2 : ty) : ty + (big ? 0 : 4);
        d.setCursor((128 - tw) / 2, yy);
        d.print(cur.text);
        d.setTextSize(1);
        if (cur.sub[0]) {
            d.setCursor((128 - (int)strlen(cur.sub) * 6 + 1) / 2, 36);
            d.print(cur.sub);
        }
    }
}
