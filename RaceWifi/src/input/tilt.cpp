#include "tilt.h"

#include <math.h>

#include "../config.h"

float lateralAngleDeg(const float a[3], int axis, int sign) {
    float lat = (float)sign * a[axis];
    float o1 = a[(axis + 1) % 3], o2 = a[(axis + 2) % 3];
    return atan2f(lat, sqrtf(o1 * o1 + o2 * o2)) * 57.2957795f;
}

// ------------------------------------------------------------------ Tilt

void Tilt::setNeutral(const float a[3]) {
    for (int i = 0; i < 3; i++) neutral_[i] = a[i];
    reconfigure();
    filt_ = raw_ = steer_ = 0;
}

void Tilt::reconfigure() { neutralDeg_ = lateralAngleDeg(neutral_, params.axis, params.sign); }

void Tilt::update(const ImuSample& s, float dt) {
    float a[3] = {s.ax, s.ay, s.az};
    raw_ = lateralAngleDeg(a, params.axis, params.sign) - neutralDeg_;
    float dtMs = dt * 1000.0f;
    filt_ += (raw_ - filt_) * (dtMs / (params.filterMs + dtMs));
    float mag = fabsf(filt_);
    float span = params.maxDeg - params.deadzoneDeg;
    float st = (mag <= params.deadzoneDeg || span <= 0) ? 0.0f : (mag - params.deadzoneDeg) / span;
    if (st > 1.0f) st = 1.0f;
    steer_ = filt_ < 0 ? -st : st;
}

// ------------------------------------------------------------------ Calibrator

void Calibrator::reset(uint32_t now) {
    done_ = false;
    movedMs_ = 0;
    restart(now);
}

void Calibrator::restart(uint32_t now) {
    startMs_ = now;
    n_ = 0;
    for (int i = 0; i < 3; i++) sum_[i] = sq_[i] = 0;
}

Calibrator::Status Calibrator::add(const ImuSample& s, uint32_t now) {
    if (done_) return Done;
    if (s.count == lastCount_) return Collecting;  // misma lectura que el tick anterior
    lastCount_ = s.count;
    float a[3] = {s.ax, s.ay, s.az};
    if (n_ >= 5) {
        for (int i = 0; i < 3; i++) {
            float m = (float)(sum_[i] / n_);
            if (fabsf(a[i] - m) > 4.0f * CALIB_STILL_G) {
                movedMs_ = now;
                restart(now);
                return Moving;
            }
        }
    }
    for (int i = 0; i < 3; i++) {
        sum_[i] += a[i];
        sq_[i] += (double)a[i] * a[i];
    }
    n_++;
    if (now - startMs_ < CALIB_WINDOW_MS || n_ < 20) return Collecting;

    float worst = 0, mag2 = 0;
    for (int i = 0; i < 3; i++) {
        double m = sum_[i] / n_;
        double var = sq_[i] / n_ - m * m;
        float sd = var > 0 ? (float)sqrt(var) : 0.0f;
        if (sd > worst) worst = sd;
        mean_[i] = (float)m;
        mag2 += mean_[i] * mean_[i];
    }
    float mag = sqrtf(mag2);
    if (worst > CALIB_STILL_G || mag < 0.8f || mag > 1.2f) {
        movedMs_ = now;
        restart(now);
        return Moving;
    }
    done_ = true;
    return Done;
}

float Calibrator::progress(uint32_t now) const {
    if (done_) return 1.0f;
    float p = (float)(now - startMs_) / (float)CALIB_WINDOW_MS;
    return p > 1.0f ? 1.0f : p;
}

// ------------------------------------------------------------------ AxisWizard

void AxisWizard::start(const float base[3]) {
    for (int i = 0; i < 3; i++) base_[i] = base[i];
    st_ = WaitTilt;
}

AxisWizard::Status AxisWizard::add(const ImuSample& s, uint32_t now) {
    float d[3] = {s.ax - base_[0], s.ay - base_[1], s.az - base_[2]};
    int best = 0;
    for (int i = 1; i < 3; i++)
        if (fabsf(d[i]) > fabsf(d[best])) best = i;
    switch (st_) {
        case WaitTilt:
            if (fabsf(d[best]) > AXIS_WIZARD_G) {
                axis_ = best;
                sign_ = d[best] > 0 ? 1 : -1;
                holdStart_ = now;
                st_ = Holding;
            }
            break;
        case Holding:
            if (best != axis_ || d[axis_] * (float)sign_ < AXIS_WIZARD_G * 0.7f) {
                st_ = WaitTilt;
            } else if (now - holdStart_ >= AXIS_WIZARD_HOLD_MS) {
                st_ = WaitReturn;
            }
            break;
        case WaitReturn:
            if (fabsf(d[0]) < 0.1f && fabsf(d[1]) < 0.1f && fabsf(d[2]) < 0.1f) st_ = Done;
            break;
        case Done:
            break;
    }
    return st_;
}

float AxisWizard::holdProgress(uint32_t now) const {
    if (st_ == WaitReturn || st_ == Done) return 1.0f;
    if (st_ != Holding) return 0.0f;
    float p = (float)(now - holdStart_) / (float)AXIS_WIZARD_HOLD_MS;
    return p > 1.0f ? 1.0f : p;
}
