#include "TrucoAI.h"

typedef TrucoEngine TE;

// ---------------------------------------------------------------------------
//  Quick play-out of the rest of a hand (cards only, no more singing)
// ---------------------------------------------------------------------------

struct Sim {
    TCard h[2][3];
    bool played[2][3];
    int8_t table[3][2];
    int8_t tw[3];
    uint8_t trick, turn, mano;
};

static int8_t simDecided(const Sim& s) {
    const int8_t* w = s.tw;
    if (s.trick == 0) return -1;
    if (s.trick == 1) {
        if (w[0] == 2 && w[1] == 2) return -1;
        if (w[0] == 2) return w[1];
        if (w[1] == 2) return w[0];
        if (w[0] == w[1]) return w[0];
        return -1;
    }
    if (w[2] == 2) {
        if (w[0] != 2) return w[0];
        if (w[1] != 2) return w[1];
        return s.mano;
    }
    return w[2];
}

// Simple, reasonable play: second in the trick, the cheapest card that wins
// (or the cheapest card if none wins); first in the trick, a random card in
// the first trick and the best one later.
static int simPolicy(const Sim& s, uint8_t p) {
    int8_t other = s.table[s.trick][1 - p];
    int best = -1;
    if (other >= 0) {
        uint8_t r = trucoRank(s.h[1 - p][other]);
        int win = -1, low = -1;
        for (int i = 0; i < 3; i++) {
            if (s.played[p][i]) continue;
            uint8_t ri = trucoRank(s.h[p][i]);
            if (ri > r && (win < 0 || ri < trucoRank(s.h[p][win]))) win = i;
            if (low < 0 || ri < trucoRank(s.h[p][low])) low = i;
        }
        return win >= 0 ? win : low;
    }
    if (s.trick == 0) {
        int opts[3], n = 0;
        for (int i = 0; i < 3; i++) {
            if (!s.played[p][i]) opts[n++] = i;
        }
        return opts[random(0, n)];
    }
    for (int i = 0; i < 3; i++) {
        if (s.played[p][i]) continue;
        if (best < 0 || trucoRank(s.h[p][i]) > trucoRank(s.h[p][best])) best = i;
    }
    return best;
}

static uint8_t playOut(Sim& s, uint8_t me, int forced) {
    for (int guard = 0; guard < 8; guard++) {
        uint8_t p = s.turn;
        int c = (forced >= 0 && p == me) ? forced : simPolicy(s, p);
        if (p == me) forced = -1;
        s.played[p][c] = true;
        s.table[s.trick][p] = c;
        if (s.table[s.trick][1 - p] < 0) {
            s.turn = 1 - p;
            continue;
        }
        uint8_t r0 = trucoRank(s.h[0][s.table[s.trick][0]]);
        uint8_t r1 = trucoRank(s.h[1][s.table[s.trick][1]]);
        int8_t w = r0 > r1 ? 0 : r1 > r0 ? 1 : 2;
        s.tw[s.trick] = w;
        int8_t d = simDecided(s);
        if (d >= 0) return d;
        s.trick++;
        s.turn = (w == 2) ? s.mano : w;
    }
    return s.mano;   // not reached
}

float TrucoAI::winChance(const TrucoEngine& g, uint8_t me, int card, int samples) {
    uint8_t opp = 1 - me;

    // cards it has not seen
    TCard pool[40];
    int poolN = 0;
    static const uint8_t NUMS[10] = {1, 2, 3, 4, 5, 6, 7, 10, 11, 12};
    for (uint8_t su = 0; su < 4; su++) {
        for (uint8_t n = 0; n < 10; n++) {
            TCard c = {NUMS[n], su};
            bool known = false;
            for (int i = 0; i < 3 && !known; i++) {
                if (g.hand[me][i].num == c.num && g.hand[me][i].suit == c.suit) known = true;
                if (g.played[opp][i] && g.hand[opp][i].num == c.num && g.hand[opp][i].suit == c.suit) known = true;
            }
            if (!known) pool[poolN++] = c;
        }
    }

    int wins = 0;
    for (int s = 0; s < samples; s++) {
        Sim sim;
        memcpy(sim.h[me], g.hand[me], sizeof(sim.h[me]));
        memcpy(sim.played, g.played, sizeof(sim.played));
        memcpy(sim.table, g.table, sizeof(sim.table));
        memcpy(sim.tw, g.trickWin, sizeof(sim.tw));
        sim.trick = g.trick;
        sim.turn = g.turn;
        sim.mano = g.mano;

        // the opponent's unseen cards; if it said its envido, a hand with those points
        for (int tries = 0; tries < 60; tries++) {
            int left = poolN;
            for (int i = 0; i < 3; i++) {
                if (g.played[opp][i]) {
                    sim.h[opp][i] = g.hand[opp][i];
                } else {
                    int j = random(0, left);
                    sim.h[opp][i] = pool[j];
                    TCard t = pool[j];
                    pool[j] = pool[left - 1];
                    pool[left - 1] = t;
                    left--;
                }
            }
            if (g.envShown[opp] < 0 || envidoOf(sim.h[opp], 3) == (uint8_t)g.envShown[opp]) break;
        }

        if (playOut(sim, me, card) == me) wins++;
    }
    return wins / (float)samples;
}

int TrucoAI::bestCard(const TrucoEngine& g, uint8_t me, float& p) {
    int samples = level ? 140 : 45;
    float w[3] = {-1, -1, -1};
    p = 0;
    for (int i = 0; i < 3; i++) {
        if (g.played[me][i]) continue;
        w[i] = winChance(g, me, i, samples);
        p = max(p, w[i]);
    }
    // among the cards that are (nearly) as good as the best, the cheapest:
    // the strong ones are kept for later
    int best = -1;
    for (int i = 0; i < 3; i++) {
        if (w[i] < 0 || w[i] < p - 0.03f) continue;
        if (best < 0 || trucoRank(g.hand[me][i]) < trucoRank(g.hand[me][best])) best = i;
    }
    // normal level: a mistake now and then
    if (level == 0 && rnd() < 0.12f) {
        int opts[3], n = 0;
        for (int i = 0; i < 3; i++) {
            if (!g.played[me][i]) opts[n++] = i;
        }
        best = opts[random(0, n)];
    }
    return best;
}

// ---------------------------------------------------------------------------
//  Envido
// ---------------------------------------------------------------------------

void TrucoAI::planEnvido(const TrucoEngine& g, uint8_t me) {
    int e = envidoOf(g.hand[me], 3);
    bool mano = (g.mano == me);
    float r = rnd();
    envPlan = 0;

    if (e >= 31) {
        envPlan = (r < 0.4f) ? TE::A_REAL : (r < 0.55f && e >= 32) ? TE::A_FALTA : TE::A_ENVIDO;
    } else if (e >= 28) {
        envPlan = (r < 0.72f) ? TE::A_ENVIDO : (r < 0.86f) ? TE::A_REAL : 0;
    } else if (e >= 25) {
        envPlan = (r < (mano ? 0.45f : 0.35f)) ? TE::A_ENVIDO : 0;
    } else if (r < (level ? 0.12f : 0.07f)) {
        envPlan = TE::A_ENVIDO;   // a lie
    }

    // far behind with a good envido: the falta can turn the game around
    if (e >= 30 && g.score[1 - me] >= g.score[me] + 6) envPlan = TE::A_FALTA;
    // a falta that could hand over the game only with a very good envido
    if (envPlan == TE::A_FALTA && e < 31) envPlan = TE::A_REAL;
}

TrucoEngine::Act TrucoAI::envidoAnswer(const TrucoEngine& g, uint8_t me) {
    int e = envidoOf(g.hand[me], 3);
    bool mano = (g.mano == me);

    int thr;
    if (g.chainHas(TE::A_FALTA)) {
        thr = g.faltaPoints() <= 3 ? 27 : 30;
    } else {
        uint8_t q = g.envidoQuieroPoints();
        thr = q <= 2 ? 26 : q <= 5 ? 28 : 29;
    }
    if (mano) thr -= 1;   // wins the ties
    if (level == 0) thr += random(-2, 3);

    // raise with a great envido
    if (e >= 31) {
        if (g.legal(me, TE::A_REAL) && rnd() < 0.55f) return TE::A_REAL;
        if (e >= 32 && g.legal(me, TE::A_FALTA) && rnd() < 0.5f) return TE::A_FALTA;
        if (g.legal(me, TE::A_ENVIDO) && rnd() < 0.5f) return TE::A_ENVIDO;
    } else if (e >= 29 && g.legal(me, TE::A_ENVIDO) && rnd() < 0.35f) {
        return TE::A_ENVIDO;
    }

    // a lie: raise with nothing (only on a first, plain envido)
    if (e <= 22 && g.envLen == 1 && g.legal(me, TE::A_REAL) && rnd() < (level ? 0.07f : 0.04f)) {
        return TE::A_REAL;
    }

    // saying no would give the game away: nothing to lose
    if (g.score[1 - me] + g.envidoNoQuieroPoints() >= g.target) return TE::A_QUIERO;

    return e >= thr ? TE::A_QUIERO : TE::A_NOQUIERO;
}

// ---------------------------------------------------------------------------
//  Truco
// ---------------------------------------------------------------------------

TrucoEngine::Act TrucoAI::trucoAnswer(const TrucoEngine& g, uint8_t me) {
    uint8_t L = g.trucoPending;
    float p;
    if (g.turn == me) bestCard(g, me, p);
    else p = winChance(g, me, -1, level ? 160 : 50);

    // the call itself says something (unless it is a lie...)
    float pe = p - (level ? 0.06f : 0.02f);

    float raiseThr = (L == 2) ? 0.78f : 0.86f;
    float accThr = (L == 2) ? 0.40f : (L == 3) ? 0.47f : 0.53f;
    if (level == 0) accThr += (rnd() - 0.5f) * 0.16f;

    // saying no would give the game away: nothing to lose
    if (g.score[1 - me] + (L - 1) >= g.target) {
        return (L < 4 && pe >= raiseThr) ? TE::A_TRUCO : TE::A_QUIERO;
    }
    if (L < 4 && pe >= raiseThr) return TE::A_TRUCO;
    if (pe >= accThr) return TE::A_QUIERO;
    // a lie: raise with nothing
    if (L < 4 && pe < 0.25f && !bluffing && rnd() < (level ? 0.07f : 0.04f)) {
        bluffing = true;
        return TE::A_TRUCO;
    }
    return TE::A_NOQUIERO;
}

TrucoEngine::Act TrucoAI::decide(const TrucoEngine& g, uint8_t me) {
    if (g.handNo != planHand) {
        planHand = g.handNo;
        bluffing = false;
        planEnvido(g, me);
    }

    switch (g.phase) {
        case TE::PH_RESP_ENVIDO:
            return envidoAnswer(g, me);

        case TE::PH_RESP_TRUCO:
            // "el envido está primero"
            if (envPlan && g.legal(me, (TE::Act)envPlan)) {
                TE::Act a = (TE::Act)envPlan;
                envPlan = 0;
                return a;
            }
            return trucoAnswer(g, me);

        case TE::PH_PLAY: {
            if (envPlan && g.envidoAvailable(me) && g.legal(me, (TE::Act)envPlan)) {
                TE::Act a = (TE::Act)envPlan;
                envPlan = 0;
                return a;
            }
            float p;
            int c = bestCard(g, me, p);

            if (g.legal(me, TE::A_TRUCO)) {
                uint8_t L = g.trucoLevel + 1;
                float thr = (L == 2) ? 0.68f : (L == 3) ? 0.76f : 0.84f;
                bool call = false;
                if (p >= thr) {
                    // a great hand in the first trick: sometimes keep quiet so you sing
                    call = !(L == 2 && g.trick == 0 && p >= 0.85f && rnd() < 0.4f);
                } else if (p < 0.33f && !bluffing) {
                    float br = (L == 2) ? 0.16f : (L == 3) ? 0.08f : 0.04f;
                    if (level == 0) br *= 0.6f;
                    if (rnd() < br) {
                        call = true;
                        bluffing = true;
                    }
                }
                if (call) return TE::A_TRUCO;
            }
            if (c >= 0) return (TE::Act)(TE::A_PLAY0 + c);
            break;
        }

        default:
            break;
    }

    // should not happen: first legal option
    for (int a = 0; a < TE::A_COUNT; a++) {
        if (g.legal(me, (TE::Act)a)) return (TE::Act)a;
    }
    return TE::A_MAZO;
}
