// Inclinacion -> direccion.
//
// El giro se calcula como el angulo entre el eje lateral de la pantalla y el plano
// horizontal: atan2(a_lat, sqrt(a_o1^2 + a_o2^2)). Sirve igual si la placa se sostiene
// acostada (como bandeja) o parada (como volante), porque en ambos casos inclinar a la
// derecha mete gravedad en el eje lateral. Cual de los tres ejes del QMI8658 es el
// lateral depende de como esta soldado el chip: lo detecta AxisWizard y se guarda en NVS.
#pragma once
#include <stdint.h>

#include "../platform/platform.h"

float lateralAngleDeg(const float a[3], int axis, int sign);

struct TiltParams {
    int8_t axis = 0;
    int8_t sign = 1;
    float deadzoneDeg = 3.0f;
    float maxDeg = 25.0f;
    float filterMs = 70.0f;
};

class Tilt {
public:
    TiltParams params;

    void setNeutral(const float a[3]);
    void reconfigure();                       // recalcula el neutro tras cambiar el eje
    void update(const ImuSample& s, float dt);
    float rawDeg() const { return raw_; }     // relativo al neutro, sin filtrar
    float angleDeg() const { return filt_; }  // filtrado
    float steer() const { return steer_; }    // [-1, 1] con zona muerta y saturacion
    float neutralDeg() const { return neutralDeg_; }
    const float* neutralVec() const { return neutral_; }

private:
    float neutral_[3] = {0, 0, 1};
    float neutralDeg_ = 0;
    float raw_ = 0, filt_ = 0, steer_ = 0;
};

// Calibracion de la posicion neutra: exige la placa quieta durante una ventana.
class Calibrator {
public:
    enum Status : uint8_t { Collecting, Moving, Done };
    void reset(uint32_t now);
    Status add(const ImuSample& s, uint32_t now);
    float progress(uint32_t now) const;
    const float* mean() const { return mean_; }
    bool movedRecently(uint32_t now) const { return now - movedMs_ < 600; }

private:
    void restart(uint32_t now);
    uint32_t startMs_ = 0, movedMs_ = 0, lastCount_ = 0;
    int n_ = 0;
    double sum_[3] = {0, 0, 0}, sq_[3] = {0, 0, 0};
    float mean_[3] = {0, 0, 1};
    bool done_ = false;
};

// Deteccion del eje: el usuario inclina a la derecha y se toma el eje que mas cambia.
class AxisWizard {
public:
    enum Status : uint8_t { WaitTilt, Holding, WaitReturn, Done };
    void start(const float base[3]);
    Status add(const ImuSample& s, uint32_t now);
    Status status() const { return st_; }
    float holdProgress(uint32_t now) const;
    int axis() const { return axis_; }
    int sign() const { return sign_; }

private:
    float base_[3] = {0, 0, 1};
    int axis_ = 0, sign_ = 1;
    uint32_t holdStart_ = 0;
    Status st_ = WaitTilt;
};
