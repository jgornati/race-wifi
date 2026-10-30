#include "fx.h"

#include "../assets/sprites_data.h"
#include "../config.h"

void Fx::clear() {
    for (auto& q : p) q.alive = false;
    emitAcc_[0] = emitAcc_[1] = 0;
}

Particle* Fx::alloc() {
    Particle* oldest = &p[0];
    for (auto& q : p) {
        if (!q.alive) return &q;
        if (q.ageMs * oldest->lifeMs > oldest->ageMs * q.lifeMs) oldest = &q;
    }
    return oldest;  // pool lleno: reemplaza la particula mas vieja
}

float Fx::jitter(float amp) {
    rng_ = rng_ * 1664525u + 1013904223u;
    return ((float)((rng_ >> 8) & 0xFFFF) / 65535.0f * 2.0f - 1.0f) * amp;
}

void Fx::smoke(float s, float x, uint16_t lifeMs) {
    Particle* q = alloc();
    *q = Particle{s + jitter(3), x + jitter(3), 0, lifeMs, FX_SMOKE, true};
}

void Fx::spark(float s, float x) {
    Particle* q = alloc();
    *q = Particle{s, x, 0, 220, FX_SPARK, true};
}

void Fx::crash(float s, float x) {
    spark(s, x);
    for (int i = 0; i < 3; i++) smoke(s - 6.0f, x + jitter(8), 600);
}

void Fx::emitCar(int idx, const Car& c, float dtMs) {
    if (!c.offroad) {
        emitAcc_[idx] = 0;
        return;
    }
    emitAcc_[idx] += dtMs;
    if (emitAcc_[idx] >= 70.0f) {
        emitAcc_[idx] = 0;
        smoke(c.s - CAR_HALF_L, c.x + jitter(5));
    }
}

void Fx::update(float dtMs) {
    for (auto& q : p) {
        if (!q.alive) continue;
        q.ageMs = (uint16_t)(q.ageMs + dtMs);
        if (q.ageMs >= q.lifeMs) q.alive = false;
    }
}

uint8_t Fx::spriteFor(const Particle& q) const {
    if (q.kind == FX_SPARK) return SPR_SPARK;
    uint32_t a = (uint32_t)q.ageMs * 3;
    if (a < q.lifeMs) return SPR_SMOKE_S;
    if (a < 2u * q.lifeMs) return SPR_SMOKE_M;
    return SPR_SMOKE_L;
}
