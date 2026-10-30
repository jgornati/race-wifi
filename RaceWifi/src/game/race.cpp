#include "race.h"

#include <math.h>

#include "../config.h"

namespace {

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline void decMs(uint16_t& v, float ms) {
    v = ((float)v > ms) ? (uint16_t)((float)v - ms) : 0;
}

void clampToTrack(Car& c, const Track& t) {
    float nc = t.center(c.s);
    float lim = t.maxOffset();
    if (c.x > nc + lim) {
        c.x = nc + lim;
        if (c.vx > 0) c.vx = 0;
    } else if (c.x < nc - lim) {
        c.x = nc - lim;
        if (c.vx < 0) c.vx = 0;
    }
}

constexpr float kFinishS = Track::kLength * RACE_LAPS;

}  // namespace

void carPlaceOnGrid(Car& c, const Track& t, int slot, int numCars) {
    c = Car();
    c.s = Track::kStartS;
    float side = (numCars < 2) ? 0.0f : (slot == 0 ? -1.0f : 1.0f);
    c.x = t.center(c.s) + side * Track::kGridOffset;
}

int carLap(const Car& c) {
    float s = c.s < 0 ? 0 : c.s;
    int lap = (int)(s / Track::kLength) + 1;
    if (lap < 1) lap = 1;
    if (lap > RACE_LAPS) lap = RACE_LAPS;
    return lap;
}

bool carBraking(const Car& c) { return c.penaltyMs > 0 || (c.offroad && c.v > CAR_GRASS_VMAX + 1.0f); }

void carStep(Car& c, const Track& t, float steerIn, float dt, HitEvent* hit) {
    if (hit) hit->happened = false;
    float ms = dt * 1000.0f;
    decMs(c.penaltyMs, ms);
    decMs(c.invulnMs, ms);
    decMs(c.bumpCooldownMs, ms);

    float steer = steerIn;
    if (c.finished) {
        // tras la llegada sigue solo por el centro, mas despacio
        steer = (t.center(c.s + CAM_LOOKAHEAD) - c.x) / 30.0f;
    }
    steer = clampf(steer, -1.0f, 1.0f);
    c.steer = steer;

    c.offroad = fabsf(c.x - t.center(c.s)) > Track::kHalfWidth;
    float vmax = c.finished ? CAR_VMAX * 0.5f : CAR_VMAX;
    if (c.offroad && vmax > CAR_GRASS_VMAX) vmax = CAR_GRASS_VMAX;
    if (c.v > vmax) {
        c.v -= CAR_GRASS_DECEL * dt;
        if (c.v < vmax) c.v = vmax;
    } else if (c.penaltyMs == 0) {
        c.v += CAR_ACCEL * dt;
        if (c.v > vmax) c.v = vmax;
    }
    if (c.v < CAR_VMIN) c.v = CAR_VMIN;

    float lat = steer * CAR_LAT_MAX * (0.5f + 0.5f * c.v / CAR_VMAX);
    float k = clampf(CAR_LAT_RESP * dt, 0.0f, 1.0f);
    c.vx += (lat - c.vx) * k;
    c.x += c.vx * dt;
    c.s += c.v * dt;
    clampToTrack(c, t);

    if (c.invulnMs > 0) return;
    float ws = t.wrap(c.s);
    for (int i = 0; i < t.obstacleCount(); i++) {
        const Obstacle& o = t.obstacle(i);
        float ds = ws - o.s;
        if (ds > Track::kLength * 0.5f) ds -= Track::kLength;
        if (ds < -Track::kLength * 0.5f) ds += Track::kLength;
        if (fabsf(ds) >= CAR_HALF_L + o.halfL) continue;
        float ox = t.center(c.s - ds) + o.off;
        float dx = c.x - ox;
        float overlap = CAR_HALF_W + o.halfW - fabsf(dx);
        if (overlap <= 0) continue;

        // Choque: pierde velocidad, recibe una penalizacion breve y queda fuera del
        // obstaculo hacia el lado mas cercano. La invulnerabilidad evita que vuelva a
        // engancharse mientras recupera, asi que nunca queda trabado.
        float dir = (dx >= 0) ? 1.0f : -1.0f;
        if (hit) {
            hit->happened = true;
            hit->s = c.s - ds * 0.5f;
            hit->x = ox + dir * o.halfW;
        }
        c.v *= HIT_SPEED_FACTOR;
        if (c.v < CAR_VMIN) c.v = CAR_VMIN;
        c.penaltyMs = HIT_PENALTY_MS;
        c.invulnMs = HIT_INVULN_MS;
        c.x += dir * (overlap + 1.0f);
        c.vx = dir * 40.0f;
        c.hits++;
        clampToTrack(c, t);
        break;
    }
}

bool carsResolve(Car& a, Car& b, const Track& t, HitEvent* ev) {
    if (ev) ev->happened = false;
    if (fabsf(a.s - b.s) >= 2.0f * CAR_HALF_L) return false;
    float dx = a.x - b.x;
    float overlap = 2.0f * CAR_HALF_W - fabsf(dx);
    if (overlap <= 0) return false;

    // separacion lateral siempre, para que nunca queden encimados
    float dir = (dx > 0) ? 1.0f : (dx < 0 ? -1.0f : (a.s >= b.s ? 1.0f : -1.0f));
    float push = overlap * 0.5f + 0.5f;
    a.x += dir * push;
    b.x -= dir * push;
    a.vx = dir * 30.0f;
    b.vx = -dir * 30.0f;
    clampToTrack(a, t);
    clampToTrack(b, t);

    if (a.bumpCooldownMs > 0 || b.bumpCooldownMs > 0) return false;
    Car& rear = (a.s < b.s) ? a : b;
    Car& front = (a.s < b.s) ? b : a;
    rear.v *= BUMP_REAR_FACTOR;
    front.v *= BUMP_FRONT_FACTOR;
    if (rear.v < CAR_VMIN) rear.v = CAR_VMIN;
    if (front.v < CAR_VMIN) front.v = CAR_VMIN;
    if (rear.penaltyMs < BUMP_PENALTY_MS) rear.penaltyMs = BUMP_PENALTY_MS;
    a.bumpCooldownMs = b.bumpCooldownMs = BUMP_COOLDOWN_MS;
    a.bumps++;
    b.bumps++;
    if (ev) {
        ev->happened = true;
        ev->s = (a.s + b.s) * 0.5f;
        ev->x = (a.x + b.x) * 0.5f;
    }
    return true;
}

void Race::resetGrid(uint8_t n) {
    numCars = n;
    for (int i = 0; i < 2; i++) carPlaceOnGrid(cars[i], *t_, i, n);
    phase = Phase::Lobby;
    readyMask = 0;
    rematchMask = 0;
    paused = false;
    ticks_ = 0;
    firstFinishMs = -1;
    winner = WINNER_NONE;
}

void Race::startCountdown(uint32_t hostNow, uint32_t durationMs) {
    phase = Phase::Countdown;
    startHostMs = hostNow + durationMs;
    paused = false;
}

void Race::step(float dt, const float steer[2], uint32_t hostNow, RaceEvents& ev) {
    ev = RaceEvents();
    if (phase == Phase::Countdown) {
        if ((int32_t)(hostNow - startHostMs) < 0) return;
        phase = Phase::Race;
        ev.phaseChanged = true;
    }
    if (phase != Phase::Race || paused) return;

    ticks_++;
    for (int i = 0; i < numCars; i++) {
        Car& c = cars[i];
        float before = c.s;
        carStep(c, *t_, steer[i], dt, &ev.hit[i]);
        if (!c.finished && c.s >= kFinishS) {
            // tiempo exacto de cruce dentro del tick
            float frac = (kFinishS - before) / (c.s - before);
            c.finished = true;
            c.finishMs = (uint32_t)(((float)(ticks_ - 1) + frac) * 1000.0f / (float)SIM_HZ);
            if (firstFinishMs < 0) firstFinishMs = raceMs();
        }
    }
    if (numCars == 2) carsResolve(cars[0], cars[1], *t_, &ev.bump);

    bool all = true;
    for (int i = 0; i < numCars; i++) all = all && cars[i].finished;
    bool timeout = firstFinishMs >= 0 && raceMs() - firstFinishMs > FINISH_GRACE_MS;
    if (all || timeout) {
        phase = Phase::Results;
        ev.phaseChanged = true;
        computeWinner();
    }
}

void Race::computeWinner() {
    if (numCars < 2) {
        winner = 0;
        return;
    }
    const Car& a = cars[0];
    const Car& b = cars[1];
    if (a.finished && b.finished) {
        winner = (a.finishMs == b.finishMs) ? WINNER_TIE : (a.finishMs < b.finishMs ? 0 : 1);
    } else if (a.finished || b.finished) {
        winner = a.finished ? 0 : 1;
    } else {
        winner = leader();
    }
}

uint8_t Race::leader() const {
    if (numCars < 2) return 0;
    const Car& a = cars[0];
    const Car& b = cars[1];
    if (a.finished && b.finished) return a.finishMs <= b.finishMs ? 0 : 1;
    if (a.finished != b.finished) return a.finished ? 0 : 1;
    return a.s >= b.s ? 0 : 1;
}

int Race::position(int i) const { return (numCars < 2 || leader() == i) ? 1 : 2; }
