// Interfaz entre la logica del juego (portable) y el hardware.
// La implementacion real esta en esp32_platform.cpp; las pruebas en PC usan una falsa
// (tools/host/) que simula dos placas, la red y la pantalla.
#pragma once
#include <stddef.h>
#include <stdint.h>

struct ImuSample {
    float ax, ay, az;   // g, ejes del chip
    float gx, gy, gz;   // grados/s
    float tempC;
    uint32_t tMs;       // momento de la lectura
    uint32_t count;     // lecturas acumuladas (para medir la tasa)
};

#define RX_MAX_LEN 96
struct RxPacket {
    uint8_t mac[6];
    uint8_t len;
    int8_t rssi;
    uint32_t rxMs;      // momento de llegada, tomado dentro de la callback
    uint8_t data[RX_MAX_LEN];
};

struct NetCounters {
    uint32_t txOk, txFail, rx, rxDropped;
};

struct Settings {
    uint8_t version;
    int8_t steerAxis;   // 0..2 eje del acelerometro para el giro, -1 = sin configurar
    int8_t steerSign;   // +1 / -1
    uint8_t maxDeg;     // inclinacion para giro maximo (sensibilidad)
};

class Platform {
public:
    virtual ~Platform() {}

    // tiempo
    virtual uint32_t millis() = 0;
    virtual int64_t micros64() = 0;
    virtual uint32_t random32() = 0;

    // sensor
    virtual bool imuPresent() = 0;
    virtual uint8_t imuAddress() = 0;
    virtual uint8_t imuWhoAmI() = 0;
    virtual bool imuLatest(ImuSample& out) = 0;   // copia la ultima lectura

    // boton (nivel crudo; el antirrebote es portable)
    virtual bool buttonDown() = 0;

    // red
    virtual bool netBegin(uint8_t channel) = 0;
    virtual void netMyMac(uint8_t mac[6]) = 0;
    virtual bool netSetPeer(const uint8_t mac[6]) = 0;
    virtual void netClearPeer() = 0;
    virtual bool netSend(const uint8_t* mac, const void* data, size_t len) = 0;  // mac nullptr = broadcast
    virtual bool netReceive(RxPacket& out) = 0;    // saca un paquete de la cola, no bloquea
    virtual NetCounters netCounters() = 0;
    virtual uint8_t netChannel() = 0;

    // pantalla por tiras: 2 buffers de LCD_W x LCD_STRIP_H
    virtual uint16_t* lcdBuffer(int slot) = 0;
    virtual bool lcdSlotFree(int slot) = 0;       // no bloquea
    virtual void lcdPush(int slot, int y0, int lines) = 0;  // asincronico
    virtual bool lcdIdle() = 0;

    // configuracion persistente
    virtual bool loadSettings(Settings& s) = 0;
    virtual void saveSettings(const Settings& s) = 0;

    // diagnostico
    virtual uint32_t freeHeap() = 0;
    virtual uint32_t minFreeHeap() = 0;
    virtual uint32_t lcdSpiHz() = 0;
    virtual void log(const char* msg) = 0;
};
