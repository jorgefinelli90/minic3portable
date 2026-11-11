#ifndef TRUCO_ENGINE_H
#define TRUCO_ENGINE_H

#include <Arduino.h>

// Mini C3 v1.8.0: Argentine truco, 1 against 1, no flor, to 15 points.
//
// Pure rules (no screen): the game screen (TrucoGame) asks who has to act,
// shows the legal options and applies what the player or the computer chose.
// Everything that happens is left in a small event queue for the screen.

struct TCard {
    uint8_t num;    // 1-7, 10, 11, 12
    uint8_t suit;   // 0 espada, 1 basto, 2 oro, 3 copa
};

uint8_t trucoRank(TCard c);                  // 14 = ancho de espada ... 1 = cuatros
uint8_t envidoOf(const TCard* cards, int n);  // envido points of a hand

class TrucoEngine {
public:
    enum Act : uint8_t {
        A_PLAY0, A_PLAY1, A_PLAY2,      // play card 0 / 1 / 2 of your hand
        A_ENVIDO, A_REAL, A_FALTA,      // envido calls (also to raise)
        A_TRUCO,                        // truco / retruco / vale cuatro (next level)
        A_QUIERO, A_NOQUIERO,
        A_MAZO,
        A_COUNT
    };
    enum Phase : uint8_t { PH_PLAY, PH_RESP_ENVIDO, PH_RESP_TRUCO, PH_HAND_OVER, PH_GAME_OVER };
    enum EvType : uint8_t {
        EV_CARD,            // p played card a (index in hand)
        EV_TRICK,           // trick a finished: winner b (2 = parda)
        EV_CALL,            // p called a (Act) ; for A_TRUCO b = level 2..4
        EV_QUIERO,          // p accepted: a = 0 envido / 1 truco
        EV_NOQUIERO,        // p refused: a = 0 envido / 1 truco, b = points to the other
        EV_ENVIDO_RESULT,   // winner p, a = envido player 0, b = envido player 1, c = points
        EV_MAZO,            // p went to the mazo, b = points to the other
        EV_HAND_END,        // winner p, a = points, b = reason (0 tricks, 1 no quiero, 2 mazo)
        EV_GAME_END         // winner p
    };
    struct Event {
        uint8_t type;
        uint8_t p;
        int8_t a, b, c;
    };

    static const uint8_t HUMAN = 0;
    static const uint8_t CPU = 1;

    // --- state (read freely; change only through apply) ---
    TCard hand[2][3];
    bool played[2][3];
    int8_t table[3][2];        // [trick][player] = card index, -1 not yet
    int8_t trickWin[3];        // -1 not played, 0 / 1 winner, 2 parda
    uint8_t trick = 0;
    uint8_t turn = 0;
    uint8_t mano = 0;          // plays first in the first trick, wins ties
    uint8_t score[2] = {0, 0};
    uint8_t target = 15;
    Phase phase = PH_GAME_OVER;
    uint16_t handNo = 0;

    // envido
    uint8_t envChain[4];
    uint8_t envLen = 0;
    bool envDone = false;
    uint8_t envCaller = 0;     // who made the last envido call
    int8_t envShown[2];        // points said when an envido was accepted, -1 unknown

    // truco
    uint8_t trucoLevel = 1;    // accepted: 1 nothing, 2 truco, 3 retruco, 4 vale cuatro
    uint8_t trucoPending = 0;  // level asked and not answered yet (0 none)
    uint8_t trucoCaller = 0;
    int8_t raiseRight = -1;    // who may raise next (-1 anybody: nothing accepted yet)
    bool trucoWaiting = false; // a truco is waiting while an envido is answered

    // hand result
    uint8_t handWinner = 0, handPoints = 0, handReason = 0;

    // events since the screen last read them
    Event events[24];
    uint8_t eventCount = 0;

    void newGame(uint8_t firstMano);
    void newHand();

    int actor() const;                    // who has to act now, -1 nobody
    bool legal(uint8_t p, Act a) const;
    bool apply(uint8_t p, Act a);         // false if it was not legal
    bool envidoAvailable(uint8_t p) const;
    uint8_t faltaPoints() const;
    uint8_t envidoQuieroPoints() const;   // what an accepted envido is worth now
    uint8_t envidoNoQuieroPoints() const;
    bool chainHas(uint8_t act) const;

private:
    void push(uint8_t type, uint8_t p, int a = 0, int b = 0, int c = 0);
    int8_t decided() const;
    void endHand(uint8_t winner, uint8_t points, uint8_t reason);
    bool award(uint8_t p, uint8_t points);   // true when that ends the game
    void afterEnvido();
};

#endif
