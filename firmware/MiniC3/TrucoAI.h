#ifndef TRUCO_AI_H
#define TRUCO_AI_H

#include "TrucoEngine.h"

// Mini C3 v1.8.0: the computer player.
//
// Cards and truco: it imagines the cards you might have (only cards it has not
// seen; if you said your envido points, only hands with those points) and
// plays the rest of the hand many times to estimate how likely it is to win.
// Envido: by its points, who is mano and what has been sung.
// It lies: sometimes it sings truco or envido with a bad hand, raises with
// nothing, or keeps quiet with a great hand so you sing first.
class TrucoAI {
public:
    uint8_t level = 1;   // 0 normal, 1 difícil

    TrucoEngine::Act decide(const TrucoEngine& g, uint8_t me);

    // Win chance of the hand if `me` plays card `card` now (-1: no card
    // forced, from the current situation). Public for the tests.
    float winChance(const TrucoEngine& g, uint8_t me, int card, int samples);

private:
    uint16_t planHand = 0xFFFF;   // hand the envido plan belongs to
    int8_t envPlan = 0;           // 0 nothing, else the Act to sing
    bool bluffing = false;        // it sang truco without cards

    float rnd() { return random(0, 10000) / 10000.0f; }
    void planEnvido(const TrucoEngine& g, uint8_t me);
    TrucoEngine::Act envidoAnswer(const TrucoEngine& g, uint8_t me);
    TrucoEngine::Act trucoAnswer(const TrucoEngine& g, uint8_t me);
    int bestCard(const TrucoEngine& g, uint8_t me, float& p);
};

#endif
