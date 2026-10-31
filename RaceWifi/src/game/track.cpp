#include "track.h"

#include <math.h>

#include "../assets/sprites_data.h"
#include "../config.h"

namespace {

struct ControlPoint {
    float s, center;
};

// Recorrido de una vuelta. Entre puntos se interpola con smoothstep, asi que las
// curvas son suaves y el ultimo punto coincide con el primero (vuelta cerrada).
const ControlPoint kPoints[] = {
    {0, 0},       {600, 0},     {1000, 55},   {1500, 55},  {1900, -40}, {2200, -65},
    {2700, -65},  {3000, 0},    {3300, 50},   {3700, 50},  {4000, -30}, {4500, -30},
    {4800, 40},   {5300, 0},    {5800, -50},  {6100, 0},   {Track::kLength, 0},
};
const int kPointCount = sizeof(kPoints) / sizeof(kPoints[0]);

// Obstaculos dentro de la vuelta: siempre dejan un paso de al menos un auto.
const Obstacle kObstacles[] = {
    {800, 22, OBS_CONE, 5, 5},    {1250, -25, OBS_TIRES, 9, 7}, {1700, 0, OBS_CONE, 5, 5},
    {1740, 30, OBS_CONE, 5, 5},   {2400, 18, OBS_TIRES, 9, 7},  {2460, -22, OBS_CONE, 5, 5},
    {3100, -32, OBS_CONE, 5, 5},  {3100, 32, OBS_CONE, 5, 5},   {3550, 0, OBS_TIRES, 9, 7},
    {4200, -20, OBS_CONE, 5, 5},  {4280, 20, OBS_CONE, 5, 5},   {4360, -20, OBS_CONE, 5, 5},
    {5000, 28, OBS_TIRES, 9, 7},  {5550, -10, OBS_CONE, 5, 5},  {5900, 25, OBS_TIRES, 9, 7},
};
const int kObstacleCount = sizeof(kObstacles) / sizeof(kObstacles[0]);

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

}  // namespace

void Track::build() {
    for (int i = 0; i < kLutSize; i++) {
        float s = (float)(i * kLutStep);
        int k = 0;
        while (k < kPointCount - 2 && s >= kPoints[k + 1].s) k++;
        const ControlPoint& a = kPoints[k];
        const ControlPoint& b = kPoints[k + 1];
        float t = (s - a.s) / (b.s - a.s);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float e = t * t * (3.0f - 2.0f * t);
        float c = a.center + (b.center - a.center) * e;
        lut_[i] = (int16_t)lrintf(c * 8.0f);
    }

    // Decoracion: arboles detras del guardarrail y banderas en la meta.
    decorCount_ = 0;
    float treeBase = railInner() + kRailW + 6.0f;
    for (int i = 0; i * 120 < (int)kLength && decorCount_ < kMaxDecor - 4; i++) {
        uint32_t h = hash32((uint32_t)i * 2654435761U);
        float s = (float)(i * 120 + (h & 63));
        float side = (h & 0x100) ? 1.0f : -1.0f;
        float off = treeBase + (float)((h >> 12) & 15);
        decor_[decorCount_++] = {s, side * off, SPR_TREE};
        if (h & 0x200) {  // a veces tambien del otro lado
            decor_[decorCount_++] = {s + 50.0f, -side * (treeBase + (float)((h >> 20) & 15)), SPR_TREE};
        }
    }
    decor_[decorCount_++] = {kFinishDepth * 0.5f, -(kHalfWidth + 10.0f), SPR_FLAG};
    decor_[decorCount_++] = {kFinishDepth * 0.5f, kHalfWidth + 10.0f, SPR_FLAG};
}

float Track::wrap(float s) const {
    float w = fmodf(s, kLength);
    if (w < 0) w += kLength;
    return w;
}

float Track::center(float s) const {
    float w = wrap(s) / (float)kLutStep;
    int i = (int)w;
    if (i >= kLutSize - 1) i = kLutSize - 2;
    float f = w - (float)i;
    return ((float)lut_[i] + ((float)lut_[i + 1] - (float)lut_[i]) * f) * (1.0f / 8.0f);
}

float Track::railInner() const { return kHalfWidth + kGrassMargin + CAR_HALF_W; }

float Track::maxOffset() const { return kHalfWidth + kGrassMargin; }

int Track::obstacleCount() const { return TRACK_OBSTACLES ? kObstacleCount : 0; }

const Obstacle& Track::obstacle(int i) const { return kObstacles[i]; }
