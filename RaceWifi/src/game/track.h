// Circuito: una vuelta de largo fijo que se repite. La coordenada s crece hacia
// adelante (arriba en pantalla) y x es lateral en pixeles de mundo. El centro del
// asfalto varia suavemente con s; todo es deterministico e igual en ambas placas.
#pragma once
#include <stdint.h>

enum ObstacleType : uint8_t { OBS_CONE = 0, OBS_TIRES = 1 };

struct Obstacle {
    float s;       // posicion dentro de la vuelta
    float off;     // desplazamiento respecto del centro del asfalto
    uint8_t type;
    float halfW, halfL;
};

struct Decor {
    float s, off;
    uint8_t sprite;  // SpriteId
};

class Track {
public:
    static constexpr float kLength = 6400.0f;
    static constexpr float kHalfWidth = 50.0f;     // borde exterior de los pianos
    static constexpr float kCurbW = 6.0f;
    static constexpr float kGrassMargin = 18.0f;   // pasto transitable mas alla del piano
    static constexpr float kRailW = 3.0f;
    static constexpr float kFinishDepth = 12.0f;   // alto de la franja a cuadros
    static constexpr float kStartS = -40.0f;       // largada, detras de la linea
    static constexpr float kGridOffset = 18.0f;    // separacion lateral en la largada
    static constexpr int kLutStep = 4;
    static constexpr int kLutSize = (int)(kLength / kLutStep) + 1;
    static constexpr int kMaxDecor = 128;

    void build();
    float wrap(float s) const;             // lleva s a [0, kLength)
    float center(float s) const;           // centro del asfalto (x de mundo)
    float railInner() const;               // distancia desde el centro al guardarrail
    float maxOffset() const;               // limite lateral del centro del auto

    int obstacleCount() const;
    const Obstacle& obstacle(int i) const;
    int decorCount() const { return decorCount_; }
    const Decor& decor(int i) const { return decor_[i]; }

private:
    int16_t lut_[kLutSize];  // centro en 1/8 de pixel
    Decor decor_[kMaxDecor];
    int decorCount_ = 0;
};
