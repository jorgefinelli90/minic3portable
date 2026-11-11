#include "GameService.h"

#include <Adafruit_SH110X.h>

#include "DisplayService.h"

void GameService::begin() {
    arkanoid.begin();
    truco.begin();
    for (PolledGame* g : polled) g->begin();
}

bool GameService::handleInput(InputEvent e) {
    if (inGame()) {
        // A is read straight from the pad in tickGame() (it has to work
        // while an arrow is held); the events of the pad are not used
        if (e == INPUT_TOUCH_TAP || e == INPUT_TOUCH_HOLD) return true;
        if (arkanoid.active()) arkanoid.onEvent(e);
        else if (truco.active()) truco.onEvent(e);
        // the polled games read the keys straight from the hardware
        return true;
    }

    if (swallowTap && (e == INPUT_TOUCH_TAP || e == INPUT_TOUCH_HOLD)) {
        swallowTap = false;
        if (millis() - exitedAt < 3000) return true;   // the press that left the game
    }

    uint32_t now = millis();
    switch (e) {
        case INPUT_TOUCH_TAP:
            if (selected == 0) arkanoid.start(now);
            else if (selected == 1) truco.start(now);
            else polled[selected - 2]->start(now);
            lastFrame = 0;
            touchWas = true;
            return true;
        case INPUT_LEFT:
        case INPUT_RIGHT:
            slideDir = (e == INPUT_RIGHT) ? 1 : -1;
            selected = (selected + (e == INPUT_RIGHT ? 1 : GAME_COUNT - 1)) % GAME_COUNT;
            menuShownAt = now;
            return true;
        case INPUT_TOUCH_HOLD:
            if (selected == 1) {
                truco.toggleLevel();
                menuShownAt = now - 400;   // no title drop, just redraw
                return true;
            }
            return false;   // A held = auto cycle
        default:
            return false;   // ↑ / ↓ change screen
    }
}

void GameService::gameEnded(MiniOled& oled, uint32_t now, bool aDown) {
    exitedAt = now;
    swallowTap = aDown;   // left with A: its release must not start another game
    menuShownAt = now;
    demoLast = now;
    slideDir = 0;
    drawMenu(oled, now);
}

void GameService::tickGame(MiniOled& oled, const HardwareService& hw) {
    uint32_t now = millis();

    if (arkanoid.active()) {
        if (now - lastFrame < 16) return;   // ~60 frames per second
        lastFrame = now;
        arkanoid.frame(oled, hw.isDown(INPUT_LEFT), hw.isDown(INPUT_RIGHT), hw.touchDown(), now);
        if (!arkanoid.active()) gameEnded(oled, now, hw.touchDown());
        oled.displayChanged();
        return;
    }

    if (PolledGame* g = activePolled()) {
        if (now - lastFrame < g->frameMs()) return;
        lastFrame = now;
        GameKeys k = {hw.isDown(INPUT_UP), hw.isDown(INPUT_DOWN), hw.isDown(INPUT_LEFT), hw.isDown(INPUT_RIGHT), hw.touchDown()};
        g->frame(oled, k, now);
        if (!inGame()) gameEnded(oled, now, hw.touchDown());
        oled.displayChanged();
        return;
    }

    if (truco.active()) {
        if (now - lastFrame < 30) return;
        lastFrame = now;
        bool t = hw.touchDown();
        if (t && !touchWas) truco.onA(now);   // A acts as soon as it is touched
        touchWas = t;
        truco.frame(oled, now);
        if (!truco.active()) gameEnded(oled, now, hw.touchDown());
        oled.displayChanged();
    }
}

void GameService::tickMenu(MiniOled& oled) {
    uint32_t now = millis();
    if (now - lastMenuFrame < 40) return;
    if (now - lastMenuFrame > 500) {
        menuShownAt = now;   // just arrived on the screen
        demoLast = now;
        slideDir = 0;
    }
    lastMenuFrame = now;
    drawMenu(oled, now);
    oled.displayChanged();
}

void GameService::drawScreen(Adafruit_GFX& d) {
    uint32_t now = millis();
    if (arkanoid.active()) arkanoid.draw(d, now);
    else if (truco.active()) truco.draw(d, now);
    else if (PolledGame* g = activePolled()) g->draw(d, now);
    else drawMenu(d, now);
}

// ---------------------------------------------------------------------------
//  Menu
// ---------------------------------------------------------------------------

static const int DEMO_L = 12, DEMO_R = 115, DEMO_T = 31, DEMO_BRICK_Y = 33, DEMO_PAD_Y = 50;

void GameService::stepDemo(uint32_t now) {
    float dt = min(now - demoLast, (uint32_t)60) / 1000.0f;
    demoLast = now;
    const float v = 48.0f;

    demoX += demoDx * v * dt;
    demoY += demoDy * v * dt;

    if (demoX < DEMO_L + 1) { demoX = DEMO_L + 1; demoDx = fabsf(demoDx); }
    if (demoX > DEMO_R - 2) { demoX = DEMO_R - 2; demoDx = -fabsf(demoDx); }
    if (demoY < DEMO_T + 1) { demoY = DEMO_T + 1; demoDy = fabsf(demoDy); }

    // the row of mini bricks
    if (demoDy < 0 && demoY <= DEMO_BRICK_Y + 3 && demoY > DEMO_BRICK_Y - 1) {
        int col = ((int)demoX + 1 - 14) / 10;
        if (col >= 0 && col < 10 && (demoBricks & (1 << col))) {
            demoBricks &= ~(1 << col);
            demoDy = fabsf(demoDy);
            demoY = DEMO_BRICK_Y + 3;
        }
    }
    if (demoBricks == 0) demoBricks = 0x3FF;

    // the paddle follows the ball (a bit late, so it looks played)
    float target = demoX + 1 - 7 + sinf(now / 700.0f) * 4;
    float maxMove = 70.0f * dt;
    demoPad += constrain(target - demoPad, -maxMove, maxMove);
    demoPad = constrain(demoPad, (float)DEMO_L + 1, (float)DEMO_R - 14);

    if (demoDy > 0 && demoY + 2 >= DEMO_PAD_Y && demoY + 2 < DEMO_PAD_Y + 2 && demoX + 2 > demoPad && demoX < demoPad + 14) {
        float rel = constrain(((demoX + 1) - (demoPad + 7)) / 8.0f, -1.0f, 1.0f);
        float ang = rel * 0.9f;
        demoDx = sinf(ang);
        demoDy = -cosf(ang);
        if (fabsf(demoDx) < 0.25f) demoDx = (demoDx < 0 ? -0.25f : 0.25f);
        demoY = DEMO_PAD_Y - 2;
    }
    if (demoY > 54) {   // missed: serve again
        demoX = demoPad + 6;
        demoY = DEMO_PAD_Y - 3;
        demoDy = -0.8f;
        demoDx = 0.6f;
    }
}

static void startButton(Adafruit_GFX& d, uint32_t now, uint32_t t, int ox) {
    if ((now / 500) % 2 == 0 || t < 600) {
        d.fillRoundRect(80 + ox, 54, 48, 10, 2, SH110X_WHITE);
        d.setTextColor(SH110X_BLACK);
        d.setCursor(83 + ox, 56);
        d.print("A jugar");
        d.setTextColor(SH110X_WHITE);
    } else {
        d.drawRoundRect(80 + ox, 54, 48, 10, 2, SH110X_WHITE);
        d.setCursor(83 + ox, 56);
        d.print("A jugar");
    }
}

static int titleDrop(uint32_t t) {
    if (t >= 350) return 0;
    float k = 1.0f - t / 350.0f;
    return -(int)(14 * k * k);
}

void GameService::drawArkanoidMenu(Adafruit_GFX& d, uint32_t now, int ox) {
    uint32_t t = now - menuShownAt;
    d.setTextSize(2);
    d.setCursor(17 + ox, 13 + (slideDir ? 0 : titleDrop(t)));
    d.print("ARKANOID");
    d.setTextSize(1);

    d.drawFastHLine(DEMO_L + ox, DEMO_T - 1, DEMO_R - DEMO_L + 1, SH110X_WHITE);
    d.drawFastVLine(DEMO_L + ox, DEMO_T - 1, DEMO_PAD_Y - DEMO_T + 5, SH110X_WHITE);
    d.drawFastVLine(DEMO_R + ox, DEMO_T - 1, DEMO_PAD_Y - DEMO_T + 5, SH110X_WHITE);
    for (int i = 0; i < 10; i++) {
        if (demoBricks & (1 << i)) d.fillRect(14 + i * 10 + ox, DEMO_BRICK_Y, 9, 3, SH110X_WHITE);
    }
    d.fillRect((int)demoX + ox, (int)demoY, 2, 2, SH110X_WHITE);
    int pad = (int)(demoPad + 0.5f) + ox;
    d.fillRect(pad + 1, DEMO_PAD_Y, 12, 2, SH110X_WHITE);
    d.drawPixel(pad, DEMO_PAD_Y + 1, SH110X_WHITE);
    d.drawPixel(pad + 13, DEMO_PAD_Y + 1, SH110X_WHITE);

    d.setCursor(0 + ox, 56);
    uint32_t rec = arkanoid.record();
    if (rec > 0) d.print("Record " + String(rec));
    else d.print("Sin record");
    startButton(d, now, t, ox);
}

void GameService::drawTrucoMenu(Adafruit_GFX& d, uint32_t now, int ox) {
    uint32_t t = now - menuShownAt;
    d.setTextSize(2);
    d.setCursor(35 + ox, 13 + (slideDir ? 0 : titleDrop(t)));
    d.print("TRUCO");
    d.setTextSize(1);

    // las tres bravas, one lifting after the other
    static const TCard SHOW[3] = {{1, 0}, {1, 1}, {7, 0}};
    int up = (now / 450) % 4;
    for (int i = 0; i < 3; i++) {
        TrucoGame::drawCard(d, 38 + i * 18 + ox, 32 - (i == up ? 3 : 0), SHOW[i]);
    }
    d.setCursor(10 + ox, 35);
    d.print("vs");
    d.setCursor(7 + ox, 44);
    d.print("CPU");
    d.setCursor(96 + ox, 35);
    d.print("G " + String(truco.wins));
    d.setCursor(96 + ox, 44);
    d.print("P " + String(truco.losses));

    d.setCursor(0 + ox, 56);
    d.print(truco.level() ? "Dificil" : "Normal");
    startButton(d, now, t, ox);
}

// Menu page of a polled game: its demo, the name on top, record and start
void GameService::drawPolledMenu(PolledGame* g, Adafruit_GFX& d, uint32_t now, int ox) {
    uint32_t t = now - menuShownAt;
    g->drawDemo(d, now, ox);

    String name = g->title();
    int size = name.length() <= 9 ? 2 : 1;
    int w = name.length() * 6 * size - size;
    int x = (128 - w) / 2 + ox;
    int y = (size == 2 ? 13 : 17) + (slideDir ? 0 : titleDrop(t));
    d.fillRect(x - 2, 11, w + 4, 18, SH110X_BLACK);
    d.setTextSize(size);
    d.setCursor(x, y);
    d.print(name);
    d.setTextSize(1);

    d.fillRect(0, 53, 128, 11, SH110X_BLACK);
    d.setCursor(0 + ox, 56);
    d.print(g->recordText());
    startButton(d, now, t, ox);
}

void GameService::drawMenu(Adafruit_GFX& d, uint32_t now) {
    stepDemo(now);

    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);

    // the new game slides in from the side of the arrow
    uint32_t t = now - menuShownAt;
    int ox = 0;
    if (slideDir != 0 && t < 220) {
        float k = 1.0f - t / 220.0f;
        ox = (int)(slideDir * 128 * k * k);
    }
    if (selected == 0) drawArkanoidMenu(d, now, ox);
    else if (selected == 1) drawTrucoMenu(d, now, ox);
    else drawPolledMenu(polled[selected - 2], d, now, ox);

    // title bar (on top of everything)
    d.fillRect(0, 0, 128, 10, SH110X_WHITE);
    d.setTextColor(SH110X_BLACK);
    d.setCursor(2, 1);
    d.print("JUEGOS");
    d.setCursor(86, 1);
    d.print(String(selected + 1) + "/" + String(GAME_COUNT));
    // little gamepad
    d.fillRoundRect(110, 2, 14, 7, 3, SH110X_BLACK);
    d.drawFastHLine(112, 5, 3, SH110X_WHITE);
    d.drawFastVLine(113, 4, 3, SH110X_WHITE);
    d.drawPixel(120, 4, SH110X_WHITE);
    d.drawPixel(121, 6, SH110X_WHITE);
    d.setTextColor(SH110X_WHITE);


    // ← → there is another game
    d.fillRect(0, 12, 6, 16, SH110X_BLACK);
    d.fillRect(122, 12, 6, 16, SH110X_BLACK);
    d.fillTriangle(1, 20, 4, 17, 4, 23, SH110X_WHITE);
    d.fillTriangle(126, 20, 123, 17, 123, 23, SH110X_WHITE);
}
