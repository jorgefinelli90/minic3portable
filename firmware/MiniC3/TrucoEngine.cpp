#include "TrucoEngine.h"

// ---------------------------------------------------------------------------
//  Cards
// ---------------------------------------------------------------------------

uint8_t trucoRank(TCard c) {
    switch (c.num) {
        case 1: return c.suit == 0 ? 14 : c.suit == 1 ? 13 : 8;   // ancho de espada, de basto, falsos
        case 7: return c.suit == 0 ? 12 : c.suit == 2 ? 11 : 4;   // siete de espada, de oro, falsos
        case 3: return 10;
        case 2: return 9;
        case 12: return 7;
        case 11: return 6;
        case 10: return 5;
        case 6: return 3;
        case 5: return 2;
        default: return 1;                                         // cuatros
    }
}

static uint8_t envidoValue(TCard c) {
    return c.num <= 7 ? c.num : 0;
}

uint8_t envidoOf(const TCard* cards, int n) {
    uint8_t best = 0;
    for (int i = 0; i < n; i++) {
        best = max(best, envidoValue(cards[i]));
        for (int j = i + 1; j < n; j++) {
            if (cards[i].suit == cards[j].suit) {
                best = max(best, (uint8_t)(20 + envidoValue(cards[i]) + envidoValue(cards[j])));
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------

void TrucoEngine::push(uint8_t type, uint8_t p, int a, int b, int c) {
    if (eventCount >= sizeof(events) / sizeof(events[0])) return;
    events[eventCount++] = {type, p, (int8_t)a, (int8_t)b, (int8_t)c};
}

void TrucoEngine::newGame(uint8_t firstMano) {
    score[0] = score[1] = 0;
    handNo = 0;
    mano = 1 - firstMano;   // newHand() switches it
    eventCount = 0;
    newHand();
}

void TrucoEngine::newHand() {
    mano = 1 - mano;
    handNo++;

    // shuffle the 40 cards and deal 3 each
    TCard deck[40];
    static const uint8_t NUMS[10] = {1, 2, 3, 4, 5, 6, 7, 10, 11, 12};
    int k = 0;
    for (uint8_t s = 0; s < 4; s++) {
        for (uint8_t n = 0; n < 10; n++) deck[k++] = {NUMS[n], s};
    }
    for (int i = 39; i > 0; i--) {
        int j = random(0, i + 1);
        TCard t = deck[i];
        deck[i] = deck[j];
        deck[j] = t;
    }
    for (int i = 0; i < 3; i++) {
        hand[mano][i] = deck[i * 2];
        hand[1 - mano][i] = deck[i * 2 + 1];
        played[0][i] = played[1][i] = false;
        table[i][0] = table[i][1] = -1;
        trickWin[i] = -1;
    }

    trick = 0;
    turn = mano;
    envLen = 0;
    envDone = false;
    envShown[0] = envShown[1] = -1;
    trucoLevel = 1;
    trucoPending = 0;
    raiseRight = -1;
    trucoWaiting = false;
    phase = PH_PLAY;
}

int TrucoEngine::actor() const {
    switch (phase) {
        case PH_PLAY: return turn;
        case PH_RESP_ENVIDO: return 1 - envCaller;
        case PH_RESP_TRUCO: return 1 - trucoCaller;
        default: return -1;
    }
}

bool TrucoEngine::chainHas(uint8_t act) const {
    for (int i = 0; i < envLen; i++) {
        if (envChain[i] == act) return true;
    }
    return false;
}

// Envido can only be sung in the first trick, before that player plays a
// card, and never once a truco has been accepted. "El envido está primero":
// it can also be the answer to a truco.
bool TrucoEngine::envidoAvailable(uint8_t p) const {
    if (envDone || envLen > 0 || trick != 0 || table[0][p] >= 0 || trucoLevel > 1) return false;
    if (phase == PH_PLAY) return turn == p && trucoPending == 0;
    if (phase == PH_RESP_TRUCO) return trucoCaller != p && trucoPending == 2;
    return false;
}

uint8_t TrucoEngine::faltaPoints() const {
    uint8_t lead = max(score[0], score[1]);
    return lead >= target ? 1 : target - lead;
}

static uint8_t envCallValue(uint8_t act) {
    return act == TrucoEngine::A_REAL ? 3 : 2;
}

uint8_t TrucoEngine::envidoQuieroPoints() const {
    if (chainHas(A_FALTA)) return faltaPoints();
    uint8_t sum = 0;
    for (int i = 0; i < envLen; i++) sum += envCallValue(envChain[i]);
    return sum;
}

uint8_t TrucoEngine::envidoNoQuieroPoints() const {
    if (envLen <= 1) return 1;
    uint8_t sum = 0;
    for (int i = 0; i < envLen - 1; i++) sum += envCallValue(envChain[i]);
    return sum;
}

bool TrucoEngine::legal(uint8_t p, Act a) const {
    if (actor() != (int)p) return false;

    switch (phase) {
        case PH_PLAY:
            switch (a) {
                case A_PLAY0: case A_PLAY1: case A_PLAY2:
                    return !played[p][a - A_PLAY0];
                case A_ENVIDO: case A_REAL: case A_FALTA:
                    return envidoAvailable(p);
                case A_TRUCO:
                    return trucoPending == 0 && trucoLevel < 4 && (raiseRight < 0 || raiseRight == (int8_t)p);
                case A_MAZO:
                    return true;
                default:
                    return false;
            }

        case PH_RESP_ENVIDO:
            switch (a) {
                case A_QUIERO: case A_NOQUIERO:
                    return true;
                case A_ENVIDO:   // envido-envido: only right after a single envido
                    return envLen == 1 && envChain[0] == A_ENVIDO;
                case A_REAL:
                    return !chainHas(A_REAL) && !chainHas(A_FALTA);
                case A_FALTA:
                    return !chainHas(A_FALTA);
                default:
                    return false;
            }

        case PH_RESP_TRUCO:
            switch (a) {
                case A_QUIERO: case A_NOQUIERO:
                    return true;
                case A_TRUCO:
                    return trucoPending < 4;
                case A_ENVIDO: case A_REAL: case A_FALTA:
                    return envidoAvailable(p);
                default:
                    return false;
            }

        default:
            return false;
    }
}

// Who wins the hand with the tricks played so far (-1 not decided yet)
int8_t TrucoEngine::decided() const {
    const int8_t* w = trickWin;
    if (trick == 0) return -1;
    if (trick == 1) {
        if (w[0] == 2 && w[1] == 2) return -1;
        if (w[0] == 2) return w[1];
        if (w[1] == 2) return w[0];
        if (w[0] == w[1]) return w[0];
        return -1;
    }
    if (w[2] == 2) {
        if (w[0] != 2) return w[0];   // the first trick decides
        if (w[1] != 2) return w[1];
        return mano;                  // all three parda
    }
    return w[2];
}

bool TrucoEngine::award(uint8_t p, uint8_t points) {
    score[p] = min(255, score[p] + points);
    if (score[p] >= target) {
        phase = PH_GAME_OVER;
        push(EV_GAME_END, p);
        return true;
    }
    return false;
}

void TrucoEngine::endHand(uint8_t winner, uint8_t points, uint8_t reason) {
    handWinner = winner;
    handPoints = points;
    handReason = reason;
    phase = PH_HAND_OVER;
    push(EV_HAND_END, winner, points, reason);
    award(winner, points);
}

void TrucoEngine::afterEnvido() {
    envDone = true;
    if (phase == PH_GAME_OVER) return;
    if (trucoWaiting) {
        trucoWaiting = false;
        phase = PH_RESP_TRUCO;   // now the truco has to be answered
    } else {
        phase = PH_PLAY;
    }
}

bool TrucoEngine::apply(uint8_t p, Act a) {
    if (!legal(p, a)) return false;
    uint8_t other = 1 - p;

    switch (a) {
        case A_PLAY0: case A_PLAY1: case A_PLAY2: {
            int idx = a - A_PLAY0;
            played[p][idx] = true;
            table[trick][p] = idx;
            push(EV_CARD, p, idx, trick);

            if (table[trick][other] < 0) {
                turn = other;
                break;
            }
            // both played: who wins the trick
            uint8_t r0 = trucoRank(hand[0][table[trick][0]]);
            uint8_t r1 = trucoRank(hand[1][table[trick][1]]);
            int8_t w = r0 > r1 ? 0 : r1 > r0 ? 1 : 2;
            trickWin[trick] = w;
            push(EV_TRICK, p, trick, w);

            int8_t d = decided();
            if (d >= 0) {
                endHand(d, trucoLevel, 0);
                break;
            }
            trick++;
            turn = (w == 2) ? mano : w;   // after a parda the mano leads
            break;
        }

        case A_ENVIDO: case A_REAL: case A_FALTA:
            if (phase == PH_RESP_TRUCO) trucoWaiting = true;   // el envido está primero
            envChain[envLen++] = a;
            envCaller = p;
            phase = PH_RESP_ENVIDO;
            push(EV_CALL, p, a);
            break;

        case A_TRUCO:
            if (phase == PH_RESP_TRUCO) trucoLevel = trucoPending;   // "quiero retruco": accepts and raises
            trucoPending = trucoLevel + 1;
            trucoCaller = p;
            phase = PH_RESP_TRUCO;
            push(EV_CALL, p, a, trucoPending);
            break;

        case A_QUIERO:
            if (phase == PH_RESP_ENVIDO) {
                uint8_t e0 = envidoOf(hand[0], 3);
                uint8_t e1 = envidoOf(hand[1], 3);
                uint8_t w = e0 > e1 ? 0 : e1 > e0 ? 1 : mano;
                uint8_t pts = envidoQuieroPoints();
                envShown[0] = e0;
                envShown[1] = e1;
                push(EV_QUIERO, p, 0);
                push(EV_ENVIDO_RESULT, w, e0, e1, pts);
                award(w, pts);
                afterEnvido();
            } else {
                trucoLevel = trucoPending;
                trucoPending = 0;
                raiseRight = p;   // only who accepted can raise next
                phase = PH_PLAY;
                push(EV_QUIERO, p, 1);
            }
            break;

        case A_NOQUIERO:
            if (phase == PH_RESP_ENVIDO) {
                uint8_t pts = envidoNoQuieroPoints();
                push(EV_NOQUIERO, p, 0, pts);
                award(envCaller, pts);
                afterEnvido();
            } else {
                uint8_t pts = trucoPending - 1;
                push(EV_NOQUIERO, p, 1, pts);
                endHand(trucoCaller, pts, 1);
            }
            break;

        case A_MAZO:
            push(EV_MAZO, p, 0, trucoLevel);
            endHand(other, trucoLevel, 2);
            break;

        default:
            return false;
    }
    return true;
}
