// RaceWifi: carreras arcade de vista cenital para dos Waveshare ESP32-S3-LCD-1.47B,
// conectadas por ESP-NOW. El mismo firmware va en las dos placas.
// Ver README.md para compilar, cargar y probar.
#include "src/app/app.h"
#include "src/platform/esp32_platform.h"

static Esp32Platform g_platform;
static App g_app;  // global: no usa la pila del loop

void setup() {
    g_platform.begin();
    g_app.begin(&g_platform);
}

void loop() { g_app.loop(); }
