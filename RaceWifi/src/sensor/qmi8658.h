// QMI8658 por I2C (Wire). Una tarea propia lo lee a IMU_HZ y deja la ultima muestra
// disponible; el juego la copia sin esperar al bus.
#pragma once
#include <stdint.h>

#include "../platform/platform.h"

namespace imu {
bool begin();          // detecta (0x6B / 0x6A, WHO_AM_I = 0x05) y configura
void startTask();
bool latest(ImuSample& out);
uint8_t address();
uint8_t whoAmI();
}  // namespace imu
