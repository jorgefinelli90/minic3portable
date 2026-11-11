#ifndef TRUCO_GAME_H
#define TRUCO_GAME_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "HardwareService.h"
#include "TrucoAI.h"
#include "TrucoEngine.h"

// Mini C3 v1.8.0: truco against the computer (screen and keys).
//
//   your turn   ← / →  choose a card · A  play it · ↓  sing (menu)
//   menu        arrows move · A choose · ↑ on the top row closes
//   when the computer sings, the answers appear by themselves
//   A during a message skips it
class TrucoGame {
public:
    void begin();                    // won / lost games and level
    void start(uint32_t now);
    bool active() const { return mode != M_OFF; }

    void onEvent(InputEvent e);      // arrows
    void onA(uint32_t now);          // A pressed (read straight from the pad)
    void frame(Adafruit_GFX& d, uint32_t now);
    void draw(Adafruit_GFX& d, uint32_t now);

    uint8_t level() const { return ai.level; }
    void toggleLevel();
    uint16_t wins = 0, losses = 0;

    // small card art, also used by the games menu
    static void drawCard(Adafruit_GFX& d, int x, int y, TCard c);
    static void drawBack(Adafruit_GFX& d, int x, int y, int w, int h);

private:
    enum Mode : uint8_t { M_OFF, M_PLAY, M_MENU, M_HAND_END, M_GAME_END };
    enum { IT_QUIT = 100 };

    struct Banner {
        char who[8];
        char text[18];
        char sub[22];
        uint16_t ms;
    };

    TrucoEngine g;
    TrucoAI ai;
    Mode mode = M_OFF;
    int sel = 0;                     // card chosen in your hand

    // menu (sing / answer)
    uint8_t items[8];
    uint8_t itemCount = 0;
    int menuSel = 0;
    bool respond = false;
    bool confirmQuit = false;
    char menuTitle[22];

    // what is on screen before the game goes on
    Banner queue[8];
    uint8_t queueN = 0;
    Banner cur;
    bool curActive = false;
    uint32_t curUntil = 0;
    int8_t flyP = -1, flyIdx = -1, flyTrick = -1;
    uint32_t flyAt = 0;
    int8_t holdTrick = -1;
    uint32_t holdUntil = 0;
    uint32_t cpuAt = 0;
    uint32_t modeAt = 0;
    bool statsSaved = false;
    int8_t lastCall = -1;            // last thing the computer sang (for the answer menu)
    int8_t lastCallLevel = 0;

    uint32_t ledUntil = 0;
    bool ledOn = false;
    uint32_t winPulseAt = 0;

    bool idle(uint32_t now) const;
    void consumeEvents(uint32_t now);
    void addBanner(const char* who, const char* text, const char* sub, uint16_t ms);
    void openMenu(bool answering);
    void chooseItem(uint32_t now);
    void humanAct(TrucoEngine::Act a, uint32_t now);
    void fixSel();
    void saveStats();
    void led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms, uint32_t now);
    void ledOff();

    const char* itemLabel(uint8_t it) const;
    static const char* callText(uint8_t act, int level);
    static void drawMicro(Adafruit_GFX& d, int x, int y, TCard c, bool invert);
    static void drawSuit(Adafruit_GFX& d, int x, int y, uint8_t suit, bool big, uint16_t color);
    void slotPos(uint8_t p, int& x, int& y) const;
};

#endif
