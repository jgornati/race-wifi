// Plataforma simulada para correr la logica real del firmware en una PC:
// tiempo virtual, red ESP-NOW con latencia/perdida, IMU con el chip rotado respecto de
// la pantalla, boton programable y LCD por tiras con la demora del SPI.
#pragma once
#include <stdint.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "../../RaceWifi/src/app/app.h"
#include "../../RaceWifi/src/platform/platform.h"

extern int64_t g_nowUs;

class FakeBoard;

class Medium {
public:
    struct InFlight {
        FakeBoard* to;
        RxPacket pkt;
        int64_t deliverUs;
    };
    void send(FakeBoard* from, const uint8_t* mac, const void* data, size_t len);
    void deliver();
    std::vector<FakeBoard*> boards;
    double loss = 0.0;
    int latMinUs = 1500, latMaxUs = 5000;
    bool blackout = false;
    uint64_t sent = 0, dropped = 0;

private:
    std::deque<InFlight> q_;
    uint32_t rng_ = 1;
};

class FakeBoard : public Platform {
public:
    FakeBoard(const char* name, const uint8_t mac[6], Medium* m);

    // ---- Platform
    uint32_t millis() override { return (uint32_t)(micros64() / 1000); }
    int64_t micros64() override { return g_nowUs + (int64_t)clockSkewMs * 1000; }
    uint32_t random32() override;
    bool imuPresent() override { return true; }
    uint8_t imuAddress() override { return 0x6B; }
    uint8_t imuWhoAmI() override { return 0x05; }
    bool imuLatest(ImuSample& out) override;
    bool buttonDown() override;
    bool netBegin(uint8_t channel) override {
        channel_ = channel;
        return true;
    }
    void netMyMac(uint8_t mac[6]) override;
    bool netSetPeer(const uint8_t mac[6]) override;
    void netClearPeer() override { hasPeer_ = false; }
    bool netSend(const uint8_t* mac, const void* data, size_t len) override;
    bool netReceive(RxPacket& out) override;
    NetCounters netCounters() override { return counters_; }
    uint8_t netChannel() override { return channel_; }
    uint16_t* lcdBuffer(int slot) override { return strip_[slot]; }
    bool lcdSlotFree(int slot) override { return micros64() >= busyUntil_[slot]; }
    void lcdPush(int slot, int y0, int lines) override;
    bool lcdIdle() override { return micros64() >= busyUntil_[0] && micros64() >= busyUntil_[1]; }
    bool loadSettings(Settings& s) override;
    void saveSettings(const Settings& s) override {
        settings_ = s;
        hasSettings_ = true;
    }
    uint32_t freeHeap() override { return 250000; }
    uint32_t minFreeHeap() override { return 240000; }
    uint32_t lcdSpiHz() override { return LCD_SPI_HZ; }
    void log(const char* msg) override;

    // ---- control de la prueba
    void press(uint32_t atMs, uint32_t durMs) { presses_.push_back({atMs, durMs}); }
    void pressNow(uint32_t durMs) { press(millis() + 5, durMs); }
    void savePpm(const std::string& path) const;
    void deliverRx(const RxPacket& p);

    App app;
    std::string name;
    uint8_t mac[6];
    bool online = true;          // false = placa "apagada" (no recibe ni manda)
    int32_t clockSkewMs = 0;     // cada placa tiene su propio reloj
    float tiltDeg = 0;           // inclinacion a la derecha que aplica el "jugador"
    float noiseG = 0.004f;
    bool quietLogs = false;
    std::vector<std::string> logs;
    uint16_t screen[LCD_H * LCD_W];
    uint32_t framesPushed = 0;

private:
    Medium* m_;
    uint8_t channel_ = 1;
    bool hasPeer_ = false;
    uint8_t peer_[6] = {0};
    NetCounters counters_ = {};
    std::deque<RxPacket> rx_;
    uint32_t rng_;
    struct Press {
        uint32_t at, dur;
    };
    std::vector<Press> presses_;
    uint16_t strip_[2][LCD_W * LCD_STRIP_H];
    int64_t busyUntil_[2] = {0, 0};
    int64_t spiFreeAt_ = 0;
    Settings settings_ = {};
    bool hasSettings_ = false;
    uint32_t imuCount_ = 0;
};
