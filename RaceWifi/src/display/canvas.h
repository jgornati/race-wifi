// Dibujo por tiras. Cada cuadro se describe con una lista fija de elementos (Frame) y el
// Renderer la rasteriza tira por tira en los dos buffers DMA de la plataforma: mientras
// una tira viaja por SPI se dibuja la siguiente. No hay asignaciones por cuadro.
#pragma once
#include <stdint.h>

#include "../assets/sprites_data.h"
#include "../config.h"

class Track;
class Platform;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace col {
constexpr uint16_t BLACK = rgb565(0, 0, 0);
constexpr uint16_t WHITE = rgb565(255, 255, 255);
constexpr uint16_t RED = rgb565(230, 40, 40);
constexpr uint16_t GREEN = rgb565(40, 220, 80);
constexpr uint16_t BLUE = rgb565(40, 110, 255);
constexpr uint16_t YELLOW = rgb565(255, 214, 0);
constexpr uint16_t ORANGE = rgb565(255, 140, 0);
constexpr uint16_t GREY = rgb565(150, 150, 160);
constexpr uint16_t DARK = rgb565(30, 32, 40);
constexpr uint16_t PANEL = rgb565(16, 18, 28);
constexpr uint16_t SHADOW = rgb565(0, 0, 0);
constexpr uint16_t CAR_SHADOW = rgb565(40, 42, 46);
constexpr uint16_t GRASS_A = rgb565(38, 110, 52);
constexpr uint16_t GRASS_B = rgb565(33, 98, 46);
constexpr uint16_t ASPHALT_A = rgb565(82, 85, 92);
constexpr uint16_t ASPHALT_B = rgb565(76, 79, 86);
constexpr uint16_t CURB_RED = rgb565(214, 40, 40);
constexpr uint16_t CURB_WHITE = rgb565(240, 240, 240);
constexpr uint16_t LANE = rgb565(220, 220, 210);
constexpr uint16_t RAIL = rgb565(190, 195, 200);
constexpr uint16_t RAIL_POST = rgb565(70, 70, 80);
}  // namespace col

enum BgMode : uint8_t { BG_SOLID, BG_TRACK, BG_TEST };
enum DrawKind : uint8_t { DK_RECT, DK_SPRITE, DK_TEXT, DK_TRI };
enum DrawFlags : uint8_t {
    DF_NONE = 0,
    DF_DITHER = 1,      // dibuja la mitad de los pixeles (fantasma / translucido)
    DF_SILHOUETTE = 2,  // sprite pintado de un solo color (sombra)
    DF_SHADOW = 4,      // texto con sombra
    DF_DOWN = 8,        // triangulo hacia abajo
};

struct DrawItem {
    uint8_t kind, flags, a, b;  // a: sprite id o escala del texto
    int16_t x, y, w, h;
    uint16_t c0, c1;
    uint16_t textOff;
};

class Frame {
public:
    static constexpr int kMaxItems = 112;
    static constexpr int kTextPool = 1024;

    void clear();
    void setSolid(uint16_t c);
    void setTrack(const Track* t, float camS, float camX, bool dim);
    void setTest();

    void rect(int x, int y, int w, int h, uint16_t c, uint8_t flags = DF_NONE);
    void outline(int x, int y, int w, int h, uint16_t c);
    void sprite(uint8_t id, int cx, int cy, uint8_t flags = DF_NONE, uint16_t tint = 0);
    int text(int x, int y, const char* s, uint16_t c, int scale = 1, uint8_t flags = DF_SHADOW);
    int textC(int y, const char* s, uint16_t c, int scale = 1, uint8_t flags = DF_SHADOW);  // centrado
    void tri(int cx, int y, int size, bool down, uint16_t c);
    static int textWidth(const char* s, int scale);

    uint8_t bg = BG_SOLID;
    uint16_t bgColor = 0;
    bool dim = false;
    const Track* track = nullptr;
    float camS = 0, camX = 0;
    int count = 0;
    int textUsed = 0;
    bool overflow = false;
    DrawItem items[kMaxItems];
    char textPool[kTextPool];

private:
    DrawItem* push();
};

class Renderer {
public:
    void begin(Platform* p) { p_ = p; }
    bool busy() const { return active_; }
    void start(const Frame& f);
    void service();  // llamar seguido desde el loop; nunca bloquea

    // metricas
    float fps = 0;
    uint32_t lastFrameUs = 0;   // desde el inicio hasta la ultima tira enviada
    uint32_t lastRenderUs = 0;  // tiempo de CPU dibujando el cuadro
    uint32_t frames = 0;

private:
    void renderStrip(uint16_t* buf, int y0, int lines);
    void backgroundRow(uint16_t* row, int y);

    Platform* p_ = nullptr;
    const Frame* f_ = nullptr;
    bool active_ = false;
    int next_ = 0;
    int64_t startUs_ = 0;
    int64_t renderUs_ = 0;
    int64_t fpsWindowUs_ = 0;
    uint32_t fpsFrames_ = 0;
};
