// Humo y chispas. Son solo visuales y locales a cada placa; no viajan por la red.
#pragma once
#include <stdint.h>

#include "race.h"

enum FxKind : uint8_t { FX_SMOKE, FX_SPARK };

struct Particle {
    float s, x;
    uint16_t ageMs, lifeMs;
    uint8_t kind;
    bool alive;
};

class Fx {
public:
    static constexpr int kMax = 40;

    void clear();
    void smoke(float s, float x, uint16_t lifeMs = 520);
    void spark(float s, float x);
    void crash(float s, float x);                    // chispa + humareda
    void emitCar(int idx, const Car& c, float dtMs); // humo al ir por el pasto
    void update(float dtMs);
    uint8_t spriteFor(const Particle& p) const;      // SpriteId segun la edad

    Particle p[kMax];

private:
    Particle* alloc();
    float jitter(float amp);
    uint32_t rng_ = 0x12345678u;
    float emitAcc_[2] = {0, 0};
};
