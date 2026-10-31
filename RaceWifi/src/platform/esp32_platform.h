// Plataforma real: Waveshare ESP32-S3-LCD-1.47B.
#pragma once
#include "platform.h"

class Esp32Platform : public Platform {
public:
    void begin();

    uint32_t millis() override;
    int64_t micros64() override;
    uint32_t random32() override;
    bool imuPresent() override { return imuOk_; }
    uint8_t imuAddress() override;
    uint8_t imuWhoAmI() override;
    bool imuLatest(ImuSample& out) override;
    bool buttonDown() override;
    void powerOff() override;
    bool netBegin(uint8_t channel) override;
    void netMyMac(uint8_t mac[6]) override;
    bool netSetPeer(const uint8_t mac[6]) override;
    void netClearPeer() override;
    bool netSend(const uint8_t* mac, const void* data, size_t len) override;
    bool netReceive(RxPacket& out) override;
    NetCounters netCounters() override;
    uint8_t netChannel() override;
    uint16_t* lcdBuffer(int slot) override;
    bool lcdSlotFree(int slot) override;
    void lcdPush(int slot, int y0, int lines) override;
    bool lcdIdle() override;
    bool loadSettings(Settings& s) override;
    void saveSettings(const Settings& s) override;
    uint32_t freeHeap() override;
    uint32_t minFreeHeap() override;
    uint32_t lcdSpiHz() override;
    void log(const char* msg) override;

private:
    bool imuOk_ = false;
    bool lcdOk_ = false;
};
