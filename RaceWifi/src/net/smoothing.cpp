#include "smoothing.h"

namespace {
inline int32_t dt(uint32_t a, uint32_t b) { return (int32_t)(a - b); }
}

void History::push(uint32_t t, const Car& c) {
    e_[head_] = {t, c.s, c.x, c.v};
    head_ = (head_ + 1) % kSize;
    if (n_ < kSize) n_++;
}

bool History::sample(uint32_t t, HistEntry& out) const {
    if (n_ == 0) return false;
    const HistEntry& first = at(0);
    const HistEntry& last = at(n_ - 1);
    if (dt(t, first.t) < 0) return false;          // mas viejo que el historial
    if (dt(t, last.t) >= 0) {
        if (dt(t, last.t) > 50) return false;      // reloj del anfitrion adelantado
        out = last;
        return true;
    }
    for (int i = n_ - 1; i > 0; i--) {
        const HistEntry& a = at(i - 1);
        const HistEntry& b = at(i);
        if (dt(t, a.t) >= 0 && dt(b.t, t) >= 0) {
            int32_t span = dt(b.t, a.t);
            float f = span > 0 ? (float)dt(t, a.t) / (float)span : 0.0f;
            out.t = t;
            out.s = a.s + (b.s - a.s) * f;
            out.x = a.x + (b.x - a.x) * f;
            out.v = a.v + (b.v - a.v) * f;
            return true;
        }
    }
    return false;
}

void History::shift(float ds, float dx, float dv) {
    for (int i = 0; i < kSize; i++) {
        e_[i].s += ds;
        e_[i].x += dx;
        e_[i].v += dv;
    }
}

void InterpBuffer::push(uint32_t t, const Car& c) {
    if (n_ > 0 && dt(t, at(n_ - 1).t) <= 0) return;   // fuera de orden: ignorar
    e_[head_] = {t, c};
    head_ = (head_ + 1) % kSize;
    if (n_ < kSize) n_++;
}

bool InterpBuffer::sample(uint32_t t, Car& out) const {
    if (n_ == 0) return false;
    const Snap& last = at(n_ - 1);
    if (dt(t, last.t) >= 0) {
        int32_t ahead = dt(t, last.t);
        if (ahead > 150) ahead = 150;
        out = last.c;
        out.s += last.c.v * (float)ahead * 0.001f;
        return true;
    }
    if (dt(t, at(0).t) <= 0) {
        out = at(0).c;
        return true;
    }
    for (int i = n_ - 1; i > 0; i--) {
        const Snap& a = at(i - 1);
        const Snap& b = at(i);
        if (dt(t, a.t) >= 0 && dt(b.t, t) >= 0) {
            int32_t span = dt(b.t, a.t);
            float f = span > 0 ? (float)dt(t, a.t) / (float)span : 0.0f;
            out = b.c;
            out.s = a.c.s + (b.c.s - a.c.s) * f;
            out.x = a.c.x + (b.c.x - a.c.x) * f;
            out.v = a.c.v + (b.c.v - a.c.v) * f;
            return true;
        }
    }
    out = last.c;
    return true;
}
