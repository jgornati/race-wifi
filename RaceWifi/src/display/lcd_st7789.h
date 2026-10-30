// Driver minimo del ST7789 de la placa (SPI2 + DMA). Dos buffers de tira: mientras uno
// se envia por DMA el juego dibuja en el otro. Nada de esto bloquea el loop.
#pragma once
#include <stdint.h>

namespace lcd {
bool begin(uint32_t spiHz);
void backlight(uint8_t pct);
uint16_t* buffer(int slot);
bool slotFree(int slot);
void push(int slot, int y0, int lines);
bool idle();
uint32_t spiHz();
}  // namespace lcd
