// Pines de la Waveshare ESP32-S3-LCD-1.47B, verificados contra:
//  - Esquematico oficial ESP32-S3-LCD-1.47B_schematic_diagram.pdf (tabla GPIO y bloque U3)
//  - Demo oficial ESP32-S3-LCD-1.47B-Demo.zip (Arduino/examples/LVGL_Arduino y ESP-IDF)
//  - https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47B
#pragma once

// LCD 1.47" ST7789, 172x320, SPI (sin MISO, sin pin TE)
#define PIN_LCD_MOSI 45
#define PIN_LCD_SCLK 40
#define PIN_LCD_CS 42
#define PIN_LCD_DC 41
#define PIN_LCD_RST 39
#define PIN_LCD_BL 46        // por MOSFET Q4, activo en alto (PWM)
#define LCD_COL_OFFSET 34    // la RAM del ST7789 tiene 240 columnas: (240-172)/2
#define LCD_ROW_OFFSET 0

// IMU QMI8658 (U3). SDO/SA0 a GND -> direccion 0x6B (el demo oficial usa 0x6B).
#define PIN_IMU_SDA 48
#define PIN_IMU_SCL 47
#define PIN_IMU_INT1 13      // conectado pero no usado: se lee por sondeo
#define PIN_IMU_INT2 12
#define IMU_I2C_ADDR 0x6B
#define IMU_I2C_ADDR_ALT 0x6A
#define IMU_I2C_HZ 400000

// Boton BOOT (Key2): GPIO0 con pull-up de 10k, activo en bajo. Es el unico boton de
// usuario; RESET va a EN. Mantenerlo apretado al encender entra al bootloader.
#define PIN_BOOT_BTN 0

// Otros recursos de la placa que este proyecto no usa
#define PIN_WS2812_LED 38    // WS2812B (no usado)
#define PIN_BAT_ADC 1        // divisor 200k/100k
