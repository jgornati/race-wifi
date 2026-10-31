#include "esp32_platform.h"

#include <Arduino.h>
#include <Preferences.h>
#include <driver/rtc_io.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "../config.h"
#include "../display/lcd_st7789.h"
#include "../hw/pins.h"
#include "../net/espnow_transport.h"
#include "../sensor/qmi8658.h"

void Esp32Platform::begin() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
    Serial.setTxTimeoutMs(0);  // sin PC conectada, el log no debe frenar el juego
#endif
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    lcdOk_ = lcd::begin(LCD_SPI_HZ);
    if (lcdOk_) lcd::backlight(LCD_BACKLIGHT_PCT);
    imuOk_ = imu::begin();
    if (imuOk_) imu::startTask();
    char buf[96];
    snprintf(buf, sizeof(buf), "[hw] lcd=%d (%lu Hz) imu=%d addr=0x%02X psram=%lu", lcdOk_,
             (unsigned long)LCD_SPI_HZ, imuOk_, imu::address(), (unsigned long)ESP.getPsramSize());
    log(buf);
}

uint32_t Esp32Platform::millis() { return ::millis(); }
int64_t Esp32Platform::micros64() { return esp_timer_get_time(); }
uint32_t Esp32Platform::random32() { return esp_random(); }

uint8_t Esp32Platform::imuAddress() { return imu::address(); }
uint8_t Esp32Platform::imuWhoAmI() { return imu::whoAmI(); }
bool Esp32Platform::imuLatest(ImuSample& out) { return imuOk_ && imu::latest(out); }

bool Esp32Platform::buttonDown() { return digitalRead(PIN_BOOT_BTN) == LOW; }

void Esp32Platform::powerOff() {
    log("[hw] apagando (sueno profundo); BOOT la vuelve a prender");
    delay(20);  // que salga el log
    espnow::end();
    if (imuOk_) imu::powerDown();
    if (lcdOk_) lcd::sleep();
    rgbLedWrite(PIN_WS2812_LED, 0, 0, 0);  // por si otro firmware lo dejo prendido
    // BOOT (GPIO0, pin RTC) despierta; tiene pull-up de 10k en la placa y se suma el interno
    rtc_gpio_pullup_en(GPIO_NUM_0);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
    esp_deep_sleep_start();
}

bool Esp32Platform::netBegin(uint8_t channel) { return espnow::begin(channel); }
void Esp32Platform::netMyMac(uint8_t mac[6]) { espnow::myMac(mac); }
bool Esp32Platform::netSetPeer(const uint8_t mac[6]) { return espnow::setPeer(mac); }
void Esp32Platform::netClearPeer() { espnow::clearPeer(); }
bool Esp32Platform::netSend(const uint8_t* mac, const void* data, size_t len) { return espnow::send(mac, data, len); }
bool Esp32Platform::netReceive(RxPacket& out) { return espnow::receive(out); }
NetCounters Esp32Platform::netCounters() { return espnow::counters(); }
uint8_t Esp32Platform::netChannel() { return espnow::channel(); }

uint16_t* Esp32Platform::lcdBuffer(int slot) { return lcd::buffer(slot); }
bool Esp32Platform::lcdSlotFree(int slot) { return lcdOk_ ? lcd::slotFree(slot) : true; }
void Esp32Platform::lcdPush(int slot, int y0, int lines) {
    if (lcdOk_) lcd::push(slot, y0, lines);
}
bool Esp32Platform::lcdIdle() { return lcdOk_ ? lcd::idle() : true; }
uint32_t Esp32Platform::lcdSpiHz() { return lcd::spiHz(); }

bool Esp32Platform::loadSettings(Settings& s) {
    Preferences p;
    if (!p.begin("racewifi", true)) return false;
    size_t n = p.getBytes("cfg", &s, sizeof(s));
    p.end();
    return n == sizeof(s);
}

void Esp32Platform::saveSettings(const Settings& s) {
    Preferences p;
    if (!p.begin("racewifi", false)) return;
    p.putBytes("cfg", &s, sizeof(s));
    p.end();
}

uint32_t Esp32Platform::freeHeap() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
uint32_t Esp32Platform::minFreeHeap() {
    return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void Esp32Platform::log(const char* msg) {
    char buf[200];
    snprintf(buf, sizeof(buf), "%8.3f %s", ::millis() / 1000.0, msg);
    Serial.println(buf);
}
