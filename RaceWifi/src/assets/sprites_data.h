// GENERADO por tools/sprite_convert.py - no editar a mano.
#pragma once
#include <stdint.h>

// Pixeles RGB565 en el orden nativo de uint16 (el panel se configura little-endian).
// Mascara: 1 bit por pixel, MSB = pixel de la izquierda, cada fila alineada a byte.
struct Sprite {
    uint8_t w, h;
    int8_t pivotX, pivotY;
    const uint16_t* pixels;
    const uint8_t* mask;
};

enum SpriteId : uint8_t {
    SPR_CAR_RED = 0,
    SPR_CAR_RED_LEFT = 1,
    SPR_CAR_RED_RIGHT = 2,
    SPR_CAR_RED_BRAKE = 3,
    SPR_CAR_BLUE = 4,
    SPR_CAR_BLUE_LEFT = 5,
    SPR_CAR_BLUE_RIGHT = 6,
    SPR_CAR_BLUE_BRAKE = 7,
    SPR_CONE = 8,
    SPR_TIRES = 9,
    SPR_TREE = 10,
    SPR_FLAG = 11,
    SPR_SMOKE_S = 12,
    SPR_SMOKE_M = 13,
    SPR_SMOKE_L = 14,
    SPR_SPARK = 15,
    SPR_COUNT = 16
};

extern const Sprite kSprites[SPR_COUNT];
