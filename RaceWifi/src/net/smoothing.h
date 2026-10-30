// Herramientas del cliente para que la red no se note:
//  - History: estados predichos del auto propio con su hora local, para comparar con
//    el estado oficial del anfitrion en el mismo instante (reconciliacion).
//  - InterpBuffer: estados recibidos del rival, para dibujarlo interpolado.
#pragma once
#include <stdint.h>

#include "../game/race.h"

struct HistEntry {
    uint32_t t;
    float s, x, v;
};

class History {
public:
    static constexpr int kSize = 64;  // ~1 s a 60 Hz
    void clear() { n_ = 0; }
    void push(uint32_t t, const Car& c);
    bool sample(uint32_t t, HistEntry& out) const;
    void shift(float ds, float dx, float dv);   // aplica una correccion a todo el historial
    int size() const { return n_; }

private:
    const HistEntry& at(int i) const { return e_[(head_ - n_ + i + kSize) % kSize]; }
    HistEntry e_[kSize];
    int head_ = 0, n_ = 0;
};

class InterpBuffer {
public:
    static constexpr int kSize = 8;
    void clear() { n_ = 0; }
    void push(uint32_t t, const Car& c);
    bool sample(uint32_t t, Car& out) const;   // interpola; extrapola hasta 150 ms
    bool empty() const { return n_ == 0; }

private:
    struct Snap {
        uint32_t t;
        Car c;
    };
    const Snap& at(int i) const { return e_[(head_ - n_ + i + kSize) % kSize]; }
    Snap e_[kSize];
    int head_ = 0, n_ = 0;
};
