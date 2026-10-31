#include "qmi8658.h"

#include <Arduino.h>
#include <Wire.h>

#include <atomic>

#include "../config.h"
#include "../hw/pins.h"

namespace {

// Registros (hoja de datos QMI8658 y demo oficial Gyro_QMI8658.h)
constexpr uint8_t REG_WHO_AM_I = 0x00;  // devuelve 0x05
constexpr uint8_t REG_CTRL1 = 0x02;
constexpr uint8_t REG_CTRL2 = 0x03;     // acelerometro: escala y ODR
constexpr uint8_t REG_CTRL3 = 0x04;     // giroscopo: escala y ODR
constexpr uint8_t REG_CTRL5 = 0x06;     // filtros paso bajo
constexpr uint8_t REG_CTRL6 = 0x07;     // AttitudeEngine
constexpr uint8_t REG_CTRL7 = 0x08;     // habilitacion de sensores
constexpr uint8_t REG_TEMP_L = 0x33;    // TEMP_L..GZ_H: 14 bytes consecutivos
constexpr uint8_t REG_RESET = 0x60;

constexpr float kAccLsbPerG = 8192.0f;   // +-4 g
constexpr float kGyrLsbPerDps = 128.0f;  // +-256 dps

uint8_t s_addr = 0;
uint8_t s_who = 0;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
ImuSample s_latest = {};
bool s_has = false;
std::atomic<bool> s_stopReq{false};
std::atomic<bool> s_running{false};

bool writeReg(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(s_addr);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission(true) == 0;
}

bool readRegs(uint8_t reg, uint8_t* buf, size_t n) {
    Wire.beginTransmission(s_addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint16_t)s_addr, n) != n) return false;
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
    return true;
}

bool readSample(ImuSample& s) {
    uint8_t b[14];
    if (!readRegs(REG_TEMP_L, b, sizeof(b))) return false;
    auto i16 = [&](int i) { return (int16_t)((uint16_t)b[i] | ((uint16_t)b[i + 1] << 8)); };
    s.tempC = i16(0) / 256.0f;
    s.ax = i16(2) / kAccLsbPerG;
    s.ay = i16(4) / kAccLsbPerG;
    s.az = i16(6) / kAccLsbPerG;
    s.gx = i16(8) / kGyrLsbPerDps;
    s.gy = i16(10) / kGyrLsbPerDps;
    s.gz = i16(12) / kGyrLsbPerDps;
    return true;
}

void task(void*) {
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000 / IMU_HZ) ? pdMS_TO_TICKS(1000 / IMU_HZ) : 1;
    uint32_t count = 0;
    for (;;) {
        // se detiene entre lecturas, nunca con el bus I2C tomado
        if (s_stopReq) {
            s_running = false;
            vTaskDelete(nullptr);
        }
        ImuSample s;
        if (readSample(s)) {
            s.tMs = millis();
            s.count = ++count;
            portENTER_CRITICAL(&s_mux);
            s_latest = s;
            s_has = true;
            portEXIT_CRITICAL(&s_mux);
        }
        vTaskDelayUntil(&last, period);
    }
}

}  // namespace

namespace imu {

bool begin() {
    if (!Wire.begin(PIN_IMU_SDA, PIN_IMU_SCL, IMU_I2C_HZ)) return false;
    Wire.setTimeOut(20);
    const uint8_t candidates[2] = {IMU_I2C_ADDR, IMU_I2C_ADDR_ALT};
    for (uint8_t a : candidates) {
        s_addr = a;
        uint8_t id = 0;
        if (readRegs(REG_WHO_AM_I, &id, 1) && id == 0x05) {
            s_who = id;
            break;
        }
        s_addr = 0;
    }
    if (!s_addr) return false;

    writeReg(REG_RESET, 0xB0);  // reset por software (solo al arrancar)
    delay(20);
    bool ok = true;
    ok &= writeReg(REG_CTRL1, 0x40);  // auto-incremento de direccion, little-endian
    ok &= writeReg(REG_CTRL2, 0x15);  // acel +-4 g, ODR ~250 Hz
    ok &= writeReg(REG_CTRL3, 0x45);  // giro +-256 dps, ODR ~250 Hz
    ok &= writeReg(REG_CTRL5, 0x77);  // LPF de acel y giro habilitados (modo 3)
    // CTRL6/CTRL7 con los mismos valores que setState(sensor_running) del demo oficial:
    // sin AttitudeEngine; acelerometro (bit 0) + giroscopo (bit 1) + reloj interno rapido.
    ok &= writeReg(REG_CTRL6, 0x00);
    ok &= writeReg(REG_CTRL7, 0x43);
    delay(30);
    return ok;
}

void startTask() {
    s_running = true;
    xTaskCreatePinnedToCore(task, "imu", 3072, nullptr, 3, nullptr, 0);
}

void powerDown() {
    s_stopReq = true;
    for (int i = 0; i < 50 && s_running; i++) delay(1);
    writeReg(REG_CTRL7, 0x00);  // acelerometro y giroscopo apagados
    writeReg(REG_CTRL1, 0x41);  // bit 0: apaga tambien el oscilador interno
}

bool latest(ImuSample& out) {
    portENTER_CRITICAL(&s_mux);
    bool has = s_has;
    out = s_latest;
    portEXIT_CRITICAL(&s_mux);
    return has;
}

uint8_t address() { return s_addr; }
uint8_t whoAmI() { return s_who; }

}  // namespace imu
