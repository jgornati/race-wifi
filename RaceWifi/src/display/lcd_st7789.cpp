#include "lcd_st7789.h"

#include <Arduino.h>
#include <string.h>

#include "../config.h"
#include "../hw/pins.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "soc/gpio_reg.h"

static_assert(PIN_LCD_DC >= 32 && PIN_LCD_DC < 49, "DC se maneja por los registros OUT1");

namespace {

constexpr int kStripBytes = LCD_W * LCD_STRIP_H * 2;
constexpr int kTransPerStrip = 6;

spi_device_handle_t s_dev = nullptr;
uint16_t* s_buf[2] = {nullptr, nullptr};
spi_transaction_t s_tr[2][kTransPerStrip];
bool s_busy[2] = {false, false};
int s_inflight = 0;
uint32_t s_hz = 0;

// Se ejecuta en la ISR del SPI antes de cada transaccion: linea DC (0 comando, 1 datos).
void IRAM_ATTR preTransfer(spi_transaction_t* t) {
    if ((int)(intptr_t)t->user)
        REG_WRITE(GPIO_OUT1_W1TS_REG, 1u << (PIN_LCD_DC - 32));
    else
        REG_WRITE(GPIO_OUT1_W1TC_REG, 1u << (PIN_LCD_DC - 32));
}

void sendCmd(uint8_t cmd, const uint8_t* data = nullptr, int len = 0) {
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = cmd;
    t.user = (void*)0;
    spi_device_polling_transmit(s_dev, &t);
    if (len > 0) {
        uint8_t buf[16];  // copia en RAM interna (apta para DMA)
        memcpy(buf, data, len);
        memset(&t, 0, sizeof(t));
        t.length = len * 8;
        t.tx_buffer = buf;
        t.user = (void*)1;
        spi_device_polling_transmit(s_dev, &t);
    }
}

void reap() {
    spi_transaction_t* rt;
    while (s_inflight > 0 && spi_device_get_trans_result(s_dev, &rt, 0) == ESP_OK) {
        s_inflight--;
        for (int k = 0; k < 2; k++)
            if (rt == &s_tr[k][kTransPerStrip - 1]) s_busy[k] = false;
    }
}

void prepareCmd(spi_transaction_t& t, uint8_t cmd) {
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = cmd;
    t.user = (void*)0;
}

void prepareData4(spi_transaction_t& t, uint16_t a, uint16_t b) {
    memset(&t, 0, sizeof(t));
    t.length = 32;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = a >> 8;
    t.tx_data[1] = a & 0xFF;
    t.tx_data[2] = b >> 8;
    t.tx_data[3] = b & 0xFF;
    t.user = (void*)1;
}

}  // namespace

namespace lcd {

bool begin(uint32_t hz) {
    s_hz = hz;
    pinMode(PIN_LCD_DC, OUTPUT);
    pinMode(PIN_LCD_RST, OUTPUT);

    spi_bus_config_t bus;
    memset(&bus, 0, sizeof(bus));
    bus.mosi_io_num = PIN_LCD_MOSI;
    bus.miso_io_num = -1;
    bus.sclk_io_num = PIN_LCD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kStripBytes + 16;
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    spi_device_interface_config_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.clock_speed_hz = (int)hz;
    dev.mode = 0;
    dev.spics_io_num = PIN_LCD_CS;
    dev.queue_size = 2 * kTransPerStrip;
    dev.pre_cb = preTransfer;
    dev.flags = SPI_DEVICE_NO_DUMMY;
    if (spi_bus_add_device(SPI2_HOST, &dev, &s_dev) != ESP_OK) return false;

    for (int k = 0; k < 2; k++) {
        s_buf[k] = (uint16_t*)heap_caps_malloc(kStripBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_buf[k]) return false;
        memset(s_buf[k], 0, kStripBytes);
    }

    // Reset y secuencia de inicio del demo Arduino oficial de Waveshare
    // (Display_ST7789.cpp, rama HORIZONTAL = vertical 172x320). Solo en el arranque.
    digitalWrite(PIN_LCD_RST, LOW);
    delay(20);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(120);
    sendCmd(0x11);  // salir de sleep
    delay(120);
    const uint8_t madctl[] = {LCD_MADCTL};
    sendCmd(0x36, madctl, 1);
    const uint8_t colmod[] = {0x05};  // 16 bits por pixel
    sendCmd(0x3A, colmod, 1);
    // RAMCTRL: E8 = little-endian (bit ENDIAN). E0 = mismo modo pero big-endian.
    const uint8_t ramctrl[] = {0x00, LCD_BIG_ENDIAN ? (uint8_t)0xE0 : (uint8_t)0xE8};
    sendCmd(0xB0, ramctrl, 2);
    const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    sendCmd(0xB2, porch, 5);
    const uint8_t b7[] = {0x35};
    sendCmd(0xB7, b7, 1);
    const uint8_t bb[] = {0x35};
    sendCmd(0xBB, bb, 1);
    const uint8_t c0[] = {0x2C};  // LCMCTRL: XOR de BGR y espejo X, como el demo oficial
    sendCmd(0xC0, c0, 1);
    const uint8_t c2[] = {0x01};
    sendCmd(0xC2, c2, 1);
    const uint8_t c3[] = {0x13};
    sendCmd(0xC3, c3, 1);
    const uint8_t c4[] = {0x20};
    sendCmd(0xC4, c4, 1);
    const uint8_t c6[] = {0x0F};
    sendCmd(0xC6, c6, 1);
    const uint8_t d0[] = {0xA4, 0xA1};
    sendCmd(0xD0, d0, 2);
    const uint8_t d6[] = {0xA1};
    sendCmd(0xD6, d6, 1);
    const uint8_t gp[] = {0xF0, 0x00, 0x04, 0x04, 0x04, 0x05, 0x29, 0x33, 0x3E, 0x38, 0x12, 0x12, 0x28, 0x30};
    sendCmd(0xE0, gp, 14);
    const uint8_t gn[] = {0xF0, 0x07, 0x0A, 0x0D, 0x0B, 0x07, 0x28, 0x33, 0x3E, 0x36, 0x14, 0x14, 0x29, 0x32};
    sendCmd(0xE1, gn, 14);
    sendCmd(0x21);  // inversion de color (requerida por este panel)
    sendCmd(0x11);
    delay(120);
    sendCmd(0x29);  // display on

    // pantalla en negro antes de prender la luz
    for (int y = 0; y < LCD_H; y += LCD_STRIP_H) {
        while (!slotFree(0)) {
        }
        memset(s_buf[0], 0, kStripBytes);
        push(0, y, LCD_STRIP_H);
    }
    while (!idle()) {
    }

    ledcAttach(PIN_LCD_BL, 1000, 10);
    return true;
}

void backlight(uint8_t pct) {
    if (pct > 100) pct = 100;
    ledcWrite(PIN_LCD_BL, (uint32_t)pct * 1023 / 100);
}

uint16_t* buffer(int slot) { return s_buf[slot]; }

bool slotFree(int slot) {
    reap();
    return !s_busy[slot];
}

bool idle() {
    reap();
    return s_inflight == 0;
}

uint32_t spiHz() { return s_hz; }

void push(int slot, int y0, int lines) {
    if (LCD_BIG_ENDIAN) {
        uint16_t* p = s_buf[slot];
        for (int i = 0; i < lines * LCD_W; i++) p[i] = (uint16_t)((p[i] << 8) | (p[i] >> 8));
    }
    spi_transaction_t* t = s_tr[slot];
    prepareCmd(t[0], 0x2A);  // columnas (offset de 34 por los 240 de la RAM)
    prepareData4(t[1], LCD_COL_OFFSET, LCD_COL_OFFSET + LCD_W - 1);
    prepareCmd(t[2], 0x2B);  // filas
    prepareData4(t[3], LCD_ROW_OFFSET + y0, LCD_ROW_OFFSET + y0 + lines - 1);
    prepareCmd(t[4], 0x2C);  // escribir memoria
    memset(&t[5], 0, sizeof(t[5]));
    t[5].length = lines * LCD_W * 16;
    t[5].tx_buffer = s_buf[slot];
    t[5].user = (void*)1;
    s_busy[slot] = true;
    for (int i = 0; i < kTransPerStrip; i++) {
        spi_device_queue_trans(s_dev, &t[i], portMAX_DELAY);  // nunca espera: hay lugar para 2 tiras
        s_inflight++;
    }
}

}  // namespace lcd
