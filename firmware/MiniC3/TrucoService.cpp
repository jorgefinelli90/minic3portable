#include "TrucoService.h"

#include <Adafruit_SH110X.h>
#include <Preferences.h>

#include "DisplayService.h"
#include "MiniC3.h"

// Layout (128 x 64): ellos on the left half, nosotros on the right half
static const int COL_W = 62;
static const int SQ = 13;            // square side (14 px with both edges)
static const uint32_t DRAW_MS = 200; // a new stick being drawn
static const uint32_t ERASE_MS = 450;
static const uint32_t WIN_BANNER_MS = 4000;

void TrucoService::begin() {
    Preferences p;
    p.begin("minic3_truco", true);
    ellos = p.getUChar("e", 0);
    nosotros = p.getUChar("n", 0);
    target = p.getUChar("t", 30);
    p.end();
    if (target != 15 && target != 30) target = 30;
    if (ellos > target) ellos = target;
    if (nosotros > target) nosotros = target;
}

void TrucoService::save() {
    Preferences p;
    p.begin("minic3_truco", false);
    p.putUChar("e", ellos);
    p.putUChar("n", nosotros);
    p.putUChar("t", target);
    p.end();
}

int TrucoService::winner() const {
    if (ellos >= target) return 0;
    if (nosotros >= target) return 1;
    return -1;
}

void TrucoService::showToast(const String& s, uint32_t ms) {
    toast = s;
    toastUntil = millis() + ms;
    dirty = true;
}

void TrucoService::led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
#if MINI_LED_RGB_ORDER
    rgbLedWrite(MINI_RGB_LED_PIN, g, r, b);   // the C3-Zero LED takes RGB
#else
    rgbLedWrite(MINI_RGB_LED_PIN, r, g, b);
#endif
    ledOn = true;
    ledUntil = millis() + ms;
}

void TrucoService::add(int side, int delta) {
    uint8_t& v = side ? nosotros : ellos;
    uint32_t now = millis();

    if (delta > 0) {
        if (winner() >= 0) {
            showToast("Partida terminada");
            return;
        }
        v++;
        animStick = v;
    } else {
        if (v == 0) return;
        animStick = v;   // the one that goes away
        v--;
    }
    animSide = side;
    animDir = delta;
    animAt = now;
    dirty = true;

    if (delta > 0 && target == 30 && v == 16) {
        showToast(side ? "Entramos en buenas" : "Ellos en buenas");
    }
    if (delta > 0 && v >= target) {
        winAt = now;
        led(30, 26, 0, 250);
    } else if (delta > 0) {
        led(10, 10, 10, 50);
    } else {
        led(25, 0, 0, 120);
    }
    if (winner() < 0) winAt = 0;
    save();
}

bool TrucoService::handleInput(InputEvent e) {
    uint32_t now = millis();

    // "Nueva partida?": A confirms, any other key cancels
    if (confirmUntil && (int32_t)(confirmUntil - now) > 0) {
        confirmUntil = 0;
        if (e == INPUT_TOUCH_TAP) {
            ellos = 0;
            nosotros = 0;
            winAt = 0;
            animSide = -1;
            save();
            showToast("Nueva partida a " + String(target));
            led(0, 0, 30, 150);
        }
        dirty = true;
        return true;
    }
    confirmUntil = 0;

    switch (e) {
        case INPUT_LEFT:       add(0, +1); return true;
        case INPUT_RIGHT:      add(1, +1); return true;
        case INPUT_LEFT_LONG:  add(0, -1); return true;
        case INPUT_RIGHT_LONG: add(1, -1); return true;

        case INPUT_TOUCH_HOLD:
            if (ellos || nosotros) {
                confirmUntil = now + 3000;
                dirty = true;
            } else {
                showToast("Ya esta en 0 a 0");
            }
            return true;

        case INPUT_TOUCH_TAP:
            if (ellos == 0 && nosotros == 0) {
                target = (target == 30) ? 15 : 30;
                save();
                showToast("Partida a " + String(target));
            } else {
                showToast("Mantene A: nueva");
            }
            return true;

        default:
            return false;   // ↑ / ↓ change screen
    }
}

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------

// Stick k of a square: 0 left, 1 top, 2 right, 3 bottom, 4 diagonal.
// f = how much of it is drawn (0..1), 2 px thick.
void TrucoService::drawStick(Adafruit_GFX& d, int x, int y, int k, float f) {
    int x1, y1, x2, y2, tx, ty;
    switch (k) {
        case 0: x1 = x;      y1 = y + SQ; x2 = x;      y2 = y;      tx = 1;  ty = 0;  break;
        case 1: x1 = x;      y1 = y;      x2 = x + SQ; y2 = y;      tx = 0;  ty = 1;  break;
        case 2: x1 = x + SQ; y1 = y;      x2 = x + SQ; y2 = y + SQ; tx = -1; ty = 0;  break;
        case 3: x1 = x + SQ; y1 = y + SQ; x2 = x;      y2 = y + SQ; tx = 0;  ty = -1; break;
        default: x1 = x;     y1 = y + SQ; x2 = x + SQ; y2 = y;      tx = 1;  ty = 0;  break;
    }
    int ex = x1 + (int)((x2 - x1) * f + (x2 >= x1 ? 0.5f : -0.5f));
    int ey = y1 + (int)((y2 - y1) * f + (y2 >= y1 ? 0.5f : -0.5f));
    d.drawLine(x1, y1, ex, ey, SH110X_WHITE);
    if (k == 4) {
        // diagonal: second line beside it, shortened so it stays inside
        d.drawLine(x1 + 1, y1, x1 + 1 + (int)((x2 - x1 - 1) * f), y1 - (int)((y1 - y2 - 1) * f), SH110X_WHITE);
    } else {
        d.drawLine(x1 + tx, y1 + ty, ex + tx, ey + ty, SH110X_WHITE);
    }
}

void TrucoService::drawSide(Adafruit_GFX& d, int side, uint32_t now) {
    int colX = side ? 66 : 0;
    int p = side ? nosotros : ellos;
    bool thirty = (target == 30);
    const int rowY[2] = {thirty ? 12 : 17, 32};

    // malas / buenas divider
    if (thirty) {
        for (int x = colX + 2; x < colX + COL_W - 1; x += 3) d.drawPixel(x, 28, SH110X_WHITE);
    }

    bool drawing = (animSide == side && animDir > 0 && now - animAt < DRAW_MS);
    for (int s = 1; s <= p; s++) {
        int g = (s - 1) / 5;
        int k = (s - 1) % 5;
        int x = colX + 4 + (g % 3) * 20;
        int y = rowY[g / 3];
        float f = (drawing && s == animStick) ? (now - animAt) / (float)DRAW_MS : 1.0f;
        drawStick(d, x, y, k, f);
    }
    // the one just removed blinks out
    if (animSide == side && animDir < 0 && now - animAt < ERASE_MS && ((now - animAt) / 75) % 2 == 0) {
        int s = animStick;
        int g = (s - 1) / 5;
        drawStick(d, colX + 4 + (g % 3) * 20, rowY[g / 3], (s - 1) % 5, 1.0f);
    }

    // the number
    String n = String(p);
    d.setTextSize(2);
    int w = n.length() * 12 - 2;
    int ny = thirty ? 49 : 40;
    d.setCursor(colX + (COL_W - w) / 2, ny);
    d.print(n);
    d.setTextSize(1);
}

void TrucoService::draw(Adafruit_GFX& d) {
    uint32_t now = millis();
    lastDraw = now;
    dirty = false;

    d.fillScreen(SH110X_BLACK);
    d.setFont();
    d.setTextSize(1);
    d.setTextWrap(false);
    d.setTextColor(SH110X_WHITE);

    int win = winner();
    const char* names[2] = {"ELLOS", "NOSOTROS"};
    for (int side = 0; side < 2; side++) {
        int colX = side ? 66 : 0;
        int w = strlen(names[side]) * 6 - 1;
        int x = colX + (COL_W - w) / 2;
        if (win == side) {
            d.fillRect(colX, 0, COL_W, 9, SH110X_WHITE);
            d.setTextColor(SH110X_BLACK);
        }
        d.setCursor(x, 1);
        d.print(names[side]);
        d.setTextColor(SH110X_WHITE);
    }
    d.drawFastHLine(0, 9, 128, SH110X_WHITE);
    d.drawFastVLine(63, 0, 64, SH110X_WHITE);
    d.drawFastVLine(64, 0, 64, SH110X_WHITE);

    drawSide(d, 0, now);
    drawSide(d, 1, now);

    // overlays
    auto box = [&](int y, int h, bool invert) {
        d.fillRect(10, y, 108, h, invert ? SH110X_WHITE : SH110X_BLACK);
        d.drawRect(10, y, 108, h, SH110X_WHITE);
        d.setTextColor(invert ? SH110X_BLACK : SH110X_WHITE);
    };
    auto center = [&](const String& s, int y) {
        d.setCursor((128 - ((int)s.length() * 6 - 1)) / 2, y);
        d.print(s);
    };

    if (confirmUntil && (int32_t)(confirmUntil - now) > 0) {
        box(18, 28, false);
        center("Nueva partida?", 22);
        center("A: si   otra: no", 34);
    } else if (win >= 0 && winAt && now - winAt < WIN_BANNER_MS) {
        bool inv = (now - winAt < 1500) && ((now - winAt) / 200) % 2 == 0;
        box(18, 28, inv);
        center(win == 1 ? "GANAMOS!" : "GANARON ELLOS", 22);
        center("Mantene A: nueva", 34);
    } else if (toastUntil && (int32_t)(toastUntil - now) > 0) {
        int w = toast.length() * 6 + 8;
        int x = (128 - w) / 2;
        d.fillRect(x, 24, w, 13, SH110X_BLACK);
        d.drawRect(x, 24, w, 13, SH110X_WHITE);
        center(toast, 27);
    }
    d.setTextColor(SH110X_WHITE);
}

void TrucoService::tick(MiniOled& oled) {
    uint32_t now = millis();

    if (winAt && now - winAt < 1800) {
        // the win: yellow pulses
        bool want = ((now - winAt) / 200) % 2 == 0;
        if (want && !ledOn) led(30, 26, 0, 2000);
        else if (!want && ledOn) {
            rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);
            ledOn = false;
        }
    } else if (ledOn && (int32_t)(now - ledUntil) >= 0) {
        rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);
        ledOn = false;
    }

    bool animating = (animSide >= 0 && now - animAt < ERASE_MS + 50)
                  || (winAt && now - winAt < WIN_BANNER_MS + 100)
                  || (toastUntil && now < toastUntil + 100)
                  || (confirmUntil && now < confirmUntil + 100);
    if (confirmUntil && (int32_t)(now - confirmUntil) > 0) {
        confirmUntil = 0;
        dirty = true;
    }

    if (!dirty && !(animating && now - lastDraw >= 33)) return;
    draw(oled);
    oled.displayChanged();
}
