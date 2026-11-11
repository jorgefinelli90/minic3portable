#include "GameUi.h"

#include <Adafruit_SH110X.h>
#include <Preferences.h>

#include "MiniC3.h"

namespace GameUi {

GameKeys edges(const GameKeys& k, GameKeys& prev) {
    GameKeys e = {k.up && !prev.up, k.down && !prev.down, k.left && !prev.left, k.right && !prev.right, k.a && !prev.a};
    prev = k;
    return e;
}

void Led::set(uint8_t r, uint8_t g, uint8_t b, uint16_t ms, uint32_t now) {
#if MINI_LED_RGB_ORDER
    rgbLedWrite(MINI_RGB_LED_PIN, g, r, b);
#else
    rgbLedWrite(MINI_RGB_LED_PIN, r, g, b);
#endif
    on = true;
    until = now + ms;
}

void Led::tick(uint32_t now) {
    if (on && (int32_t)(now - until) >= 0) off();
}

void Led::off() {
    if (!on) return;
    rgbLedWrite(MINI_RGB_LED_PIN, 0, 0, 0);
    on = false;
}

uint32_t loadRecord(const char* key, uint32_t def) {
    Preferences p;
    p.begin("minic3_game", true);
    uint32_t v = p.getUInt(key, def);
    p.end();
    return v;
}

void saveRecord(const char* key, uint32_t value) {
    Preferences p;
    p.begin("minic3_game", false);
    p.putUInt(key, value);
    p.end();
}

void centerText(Adafruit_GFX& d, const String& s, int y, int size) {
    d.setTextSize(size);
    d.setCursor((128 - ((int)s.length() * 6 * size - size)) / 2, y);
    d.print(s);
    d.setTextSize(1);
}

void drawPause(Adafruit_GFX& d, int sel) {
    d.fillRect(26, 16, 76, 36, SH110X_BLACK);
    d.drawRect(26, 16, 76, 36, SH110X_WHITE);
    d.fillRect(26, 16, 76, 10, SH110X_WHITE);
    d.setTextColor(SH110X_BLACK);
    d.setCursor(49, 17);
    d.print("PAUSA");
    const char* opts[2] = {"Seguir", "Salir"};
    for (int i = 0; i < 2; i++) {
        int y = 29 + i * 11;
        if (i == sel) {
            d.fillRect(28, y - 2, 72, 11, SH110X_WHITE);
            d.setTextColor(SH110X_BLACK);
        } else {
            d.setTextColor(SH110X_WHITE);
        }
        d.setCursor(34, y);
        d.print(i == sel ? "> " : "  ");
        d.print(opts[i]);
    }
    d.setTextColor(SH110X_WHITE);
}

int pauseInput(const GameKeys& e, int& sel) {
    if (e.up || e.down) sel ^= 1;
    if (e.a) return sel == 0 ? 1 : 2;
    return 0;
}

void drawOver(Adafruit_GFX& d, const char* title, const String& score, const String& recordLine,
              bool newRecord, uint32_t now, uint32_t since) {
    d.fillRect(6, 6, 116, 50, SH110X_BLACK);
    d.drawRect(6, 6, 116, 50, SH110X_WHITE);
    d.setTextColor(SH110X_WHITE);
    centerText(d, title, 9);
    int sy = 19;
    if (now - since < 300) sy = 19 - (int)(8 * (1.0f - (now - since) / 300.0f));
    centerText(d, score, sy, 2);
    if (newRecord) {
        if ((now / 300) % 2 == 0) centerText(d, "NUEVO RECORD!", 36);
    } else {
        centerText(d, recordLine, 36);
    }
    if (now - since > 700) {
        d.setCursor(14, 46);
        d.print("A otra vez \x1b menu");
    }
}

int overInput(const GameKeys& e, uint32_t now, uint32_t since) {
    if (now - since <= 700) return 0;
    if (e.a) return 1;
    if (e.left) return 2;
    return 0;
}

// 3x5 digits (+ 'k'), 3 bits per row
static const uint8_t TINY[11][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    {4, 5, 6, 5, 5},
};

int tinyWidth(const String& s) {
    return s.length() ? s.length() * 4 - 1 : 0;
}

int tinyNumber(Adafruit_GFX& d, int x, int y, const String& s, uint16_t color) {
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        int g = (c >= '0' && c <= '9') ? c - '0' : (c == 'k' ? 10 : -1);
        if (g >= 0) {
            for (int r = 0; r < 5; r++) {
                for (int b = 0; b < 3; b++) {
                    if (TINY[g][r] & (4 >> b)) d.drawPixel(x + i * 4 + b, y + r, color);
                }
            }
        }
    }
    return tinyWidth(s);
}

}   // namespace GameUi
