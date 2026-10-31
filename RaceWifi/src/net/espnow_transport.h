// Transporte ESP-NOW (sin router). Las callbacks solo copian a una cola FreeRTOS o
// suman contadores; todo el procesamiento ocurre en el loop del juego.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../platform/platform.h"

namespace espnow {
bool begin(uint8_t channel);
void end();  // apaga ESP-NOW y el WiFi (al dormir la placa)
void myMac(uint8_t mac[6]);
bool setPeer(const uint8_t mac[6]);
void clearPeer();
bool send(const uint8_t* mac, const void* data, size_t len);
bool receive(RxPacket& out);
NetCounters counters();
uint8_t channel();
}  // namespace espnow
