#ifndef ARKANOID_H
#define ARKANOID_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "HardwareService.h"

// Mini C3 v1.5.0: Arkanoid for the 128x64 OLED.
//
//   ← / →  move the paddle (it speeds up while held)
//   A      launch the ball / pause
//   ↓      pause
//   pause: ↑ ↓ choose "Seguir" / "Salir", A confirms
//
// 6 levels (then they repeat faster), bricks of 1 and 2 hits, capsules that
// fall from broken bricks (wide paddle, slow ball, 3 balls, extra life),
// particles, screen shake and the board LED flashing along.
class Arkanoid {
public:
    void begin();                // loads the record
    void start(uint32_t now);    // new game from level 1
    bool active() const { return phase != P_OFF; }
    bool isPaused() const { return phase == P_PAUSE; }
    void pause();                // from outside (message, sleep...)

    // Arrow / A events while playing (A is read in frame(), see there)
    void onEvent(InputEvent e);
    // One frame: update with the keys held right now, then draw.
    // touch = A held (read directly, so it works while an arrow is held)
    void frame(Adafruit_GFX& d, bool left, bool right, bool touch, uint32_t now);
    // Draw the current state without moving anything (screen transitions)
    void draw(Adafruit_GFX& d, uint32_t now);

    uint32_t record() const { return best; }
    uint8_t recordLevel() const { return bestLevel; }

    static const int COLS = 10;
    static const int ROWS = 5;

private:
    enum Phase : uint8_t { P_OFF, P_INTRO, P_READY, P_PLAY, P_PAUSE, P_DEAD, P_CLEAR, P_OVER };
    enum CapType : uint8_t { CAP_WIDE, CAP_SLOW, CAP_MULTI, CAP_LIFE };

    struct Ball { float x, y, dx, dy; bool alive; };
    struct Cap { float x, y; uint8_t type; bool alive; };
    struct Part { float x, y, vx, vy; uint16_t life; bool alive; };

    static const int MAX_BALLS = 3;
    static const int MAX_CAPS = 3;
    static const int MAX_PARTS = 28;

    Phase phase = P_OFF;
    Phase pausedFrom = P_PLAY;
    uint32_t phaseAt = 0;
    uint32_t lastFrame = 0;

    uint8_t bricks[ROWS][COLS];
    int bricksLeft = 0;
    int hitR = -1, hitC = -1;
    uint32_t hitAt = 0;

    Ball balls[MAX_BALLS];
    Cap caps[MAX_CAPS];
    Part parts[MAX_PARTS];

    float px = 55;            // paddle left x
    float pw = 18;            // paddle width (animates toward pwTarget)
    int pwTarget = 18;
    uint32_t wideUntil = 0;
    float holdTime = 0;

    float speed = 52;
    uint32_t score = 0;
    uint32_t shownScore = 0;  // the HUD counts up to the score
    int lives = 3;
    int level = 1;            // 1, 2, 3... (layouts repeat after 6, faster)

    uint32_t best = 0;
    uint8_t bestLevel = 0;
    bool newRecord = false;
    uint32_t clearBonus = 0;

    uint8_t dryBounces = 0;   // paddle bounces without breaking anything
    int pauseSel = 0;         // 0 Seguir, 1 Salir
    bool touchWas = true;     // A has to be released before it counts

    uint32_t shakeUntil = 0;
    int ox = 0, oy = 0;       // shake offset while drawing

    uint32_t ledUntil = 0;
    bool ledOn = false;

    void loadLevel();
    void resetPaddleAndBall();
    float baseSpeed() const;
    float maxSpeed() const;

    void update(float dt, bool left, bool right, bool pressed, uint32_t now);
    void updateBall(Ball& b, float dt, uint32_t now);
    bool brickAt(float x, float y, int& r, int& c);
    void hitBrick(int r, int c, uint32_t now);
    void dropCap(float x, float y);
    void applyCap(uint8_t type, uint32_t now);
    void burst(float x, float y, int n, float spread);
    void loseLife(uint32_t now);
    void finishGame();
    void saveRecord();
    void setPhase(Phase p, uint32_t now);

    void led(uint8_t r, uint8_t g, uint8_t b, uint16_t ms, uint32_t now);
    void ledOff();

    void drawField(Adafruit_GFX& d, uint32_t now);
    void drawHud(Adafruit_GFX& d);
    void drawBricks(Adafruit_GFX& d, uint32_t now);
    void drawPaddle(Adafruit_GFX& d, int x, int w);
    void drawCap(Adafruit_GFX& d, const Cap& c, uint32_t now);
    void drawBanner(Adafruit_GFX& d, const char* l1, const String& l2, bool invert);
    void centerText(Adafruit_GFX& d, const String& s, int y, int size = 1);
};

#endif
