#ifndef GAME_SERVICE_H
#define GAME_SERVICE_H

#include <Adafruit_GFX.h>
#include <Arduino.h>

#include "Arkanoid.h"
#include "Flappy.h"
#include "Game2048.h"
#include "GameKeys.h"
#include "Invaders.h"
#include "Minesweeper.h"
#include "PolledGame.h"
#include "Simon.h"
#include "Snake.h"
#include "Tetris.h"
#include "HardwareService.h"
#include "TrucoGame.h"

class MiniOled;

// Mini C3: the "Juegos" screen.
//
// Menu: one game at a time with a small live demo.
//   ← / →  other game · A  play · ↑ / ↓  other screens
//   Truco: A held changes the level (Normal / Dificil)
// While a game is running it takes every key except ↑ held 3 s (turn off).
class GameService {
public:
    void begin();

    // A game is running (also when paused or on its end screen)
    bool inGame() const { return arkanoid.active() || truco.active() || activePolled() != nullptr; }
    // Game paused from outside (a message arrived, it went to sleep...)
    void pause() {
        arkanoid.pause();
        for (PolledGame* g : polled) g->pause();
    }

    // Keys on the games screen. Returns true when it used the key.
    bool handleInput(InputEvent e);

    // Draws the games screen into the buffer (menu or the running game)
    void drawScreen(Adafruit_GFX& d);

    // While a game runs: one frame, pushed to the panel
    void tickGame(MiniOled& oled, const HardwareService& hw);
    // On the menu: animates the demo (~25 per second)
    void tickMenu(MiniOled& oled);

    // 0 Arkanoid, 1 Truco, then the polled games: Tetris, Flappy, Viborita,
    // 2048, Buscaminas, Invasores, Simon
    static const int POLLED_COUNT = 7;
    static const int GAME_COUNT = 2 + POLLED_COUNT;

private:
    Arkanoid arkanoid;
    TrucoGame truco;
    Tetris tetris;
    Flappy flappy;
    Snake snake;
    Game2048 g2048;
    Minesweeper mines;
    Invaders invaders;
    Simon simon;
    PolledGame* const polled[POLLED_COUNT] = {&tetris, &flappy, &snake, &g2048, &mines, &invaders, &simon};

    PolledGame* activePolled() const {
        for (PolledGame* g : polled) {
            if (g->active()) return g;
        }
        return nullptr;
    }
    int selected = 0;
    uint32_t lastFrame = 0;
    uint32_t lastMenuFrame = 0;
    uint32_t menuShownAt = 0;
    int slideDir = 0;               // menu: the new game slides in
    bool touchWas = true;           // truco: A is read straight from the pad
    // The A press that chose "Salir" / "volver" ends the game when it goes
    // down; its tap event arrives when it is released, already on the menu.
    // That one is ignored so it does not start a new game.
    uint32_t exitedAt = 0;
    bool swallowTap = false;

    // Arkanoid demo on the menu: a ball breaking a row of mini bricks
    float demoX = 30, demoY = 44, demoDx = 1, demoDy = -1, demoPad = 56;
    uint16_t demoBricks = 0x3FF;
    uint32_t demoLast = 0;

    void drawMenu(Adafruit_GFX& d, uint32_t now);
    void drawArkanoidMenu(Adafruit_GFX& d, uint32_t now, int ox);
    void drawTrucoMenu(Adafruit_GFX& d, uint32_t now, int ox);
    void drawPolledMenu(PolledGame* g, Adafruit_GFX& d, uint32_t now, int ox);
    void stepDemo(uint32_t now);
    void gameEnded(MiniOled& oled, uint32_t now, bool aDown);
};

#endif
