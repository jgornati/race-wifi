#include "protocol.h"

#include <math.h>

namespace {
inline int16_t q8(float v) {
    float r = v * 8.0f;
    if (r > 32767.0f) r = 32767.0f;
    if (r < -32768.0f) r = -32768.0f;
    return (int16_t)lrintf(r);
}
inline uint8_t ms10(uint16_t ms) { return (uint8_t)(ms / 10 > 255 ? 255 : ms / 10); }
}  // namespace

CarNet packCar(const Car& c) {
    CarNet n;
    n.s = c.s;
    n.x8 = q8(c.x);
    n.v8 = q8(c.v);
    n.vx8 = q8(c.vx);
    n.penalty10 = ms10(c.penaltyMs);
    n.invuln10 = ms10(c.invulnMs);
    n.flags = (c.offroad ? CF_OFFROAD : 0) | (c.finished ? CF_FINISHED : 0) |
              (c.steer < -0.3f ? CF_LEFT : 0) | (c.steer > 0.3f ? CF_RIGHT : 0) |
              (carBraking(c) ? CF_BRAKE : 0) | (c.invulnMs > 0 ? CF_INVULN : 0);
    n.hits = c.hits;
    n.bumps = c.bumps;
    n.lap = (uint8_t)carLap(c);
    n.finishMs = c.finishMs;
    return n;
}

void unpackCar(const CarNet& n, Car& c) {
    c.s = n.s;
    c.x = n.x8 / 8.0f;
    c.v = n.v8 / 8.0f;
    c.vx = n.vx8 / 8.0f;
    c.penaltyMs = (uint16_t)(n.penalty10 * 10);
    c.invulnMs = (uint16_t)(n.invuln10 * 10);
    c.offroad = n.flags & CF_OFFROAD;
    c.finished = n.flags & CF_FINISHED;
    c.steer = (n.flags & CF_LEFT) ? -1.0f : ((n.flags & CF_RIGHT) ? 1.0f : 0.0f);
    c.hits = n.hits;
    c.bumps = n.bumps;
    c.finishMs = n.finishMs;
}
