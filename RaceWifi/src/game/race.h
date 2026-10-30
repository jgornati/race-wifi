// Fisica arcade del auto y reglas de la carrera. Codigo portable: corre igual en el
// anfitrion (autoritativo), en el cliente (prediccion de su propio auto) y en las
// pruebas de PC.
#pragma once
#include <stdint.h>

#include "../config.h"
#include "track.h"

struct Car {
    float s = 0;    // distancia total recorrida (incluye vueltas)
    float x = 0;    // posicion lateral de mundo
    float v = 0;    // velocidad hacia adelante (px/s)
    float vx = 0;   // velocidad lateral (px/s)
    float steer = 0;
    uint16_t penaltyMs = 0;
    uint16_t invulnMs = 0;
    uint16_t bumpCooldownMs = 0;
    uint8_t hits = 0;    // choques con obstaculos
    uint8_t bumps = 0;   // choques entre autos
    bool offroad = false;
    bool finished = false;
    uint32_t finishMs = 0;
};

struct HitEvent {
    bool happened = false;
    float s = 0, x = 0;
};

void carPlaceOnGrid(Car& c, const Track& t, int slot, int numCars);
void carStep(Car& c, const Track& t, float steerIn, float dt, HitEvent* hit);
bool carsResolve(Car& a, Car& b, const Track& t, HitEvent* ev);
int carLap(const Car& c);   // 1..RACE_LAPS
bool carBraking(const Car& c);

enum class Phase : uint8_t { Lobby = 0, Countdown = 1, Race = 2, Results = 3 };

constexpr uint8_t WINNER_NONE = 0xFF;
constexpr uint8_t WINNER_TIE = 0xFE;

struct RaceEvents {
    HitEvent hit[2];
    HitEvent bump;
    bool phaseChanged = false;
};

class Race {
public:
    void init(const Track* t) { t_ = t; }
    void resetGrid(uint8_t numCars);
    void startCountdown(uint32_t hostNow, uint32_t durationMs);
    void step(float dt, const float steer[2], uint32_t hostNow, RaceEvents& ev);
    uint8_t leader() const;
    int position(int i) const;   // 1 = primero
    int32_t raceMs() const { return (int32_t)((int64_t)ticks_ * 1000 / SIM_HZ); }
    uint32_t ticks() const { return ticks_; }

    Phase phase = Phase::Lobby;
    uint8_t raceId = 0;
    uint8_t numCars = 1;
    uint8_t readyMask = 0;
    uint8_t rematchMask = 0;
    bool paused = false;
    uint32_t startHostMs = 0;
    int32_t firstFinishMs = -1;
    uint8_t winner = WINNER_NONE;
    Car cars[2];

private:
    void computeWinner();
    const Track* t_ = nullptr;
    uint32_t ticks_ = 0;   // ticks de carrera transcurridos (sin pausas)
};
