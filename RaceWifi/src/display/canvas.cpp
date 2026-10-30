#include "canvas.h"

#include <math.h>
#include <string.h>

#include "../game/track.h"
#include "../platform/platform.h"
#include "font5x7.h"

namespace {

constexpr int kStrips = (LCD_H + LCD_STRIP_H - 1) / LCD_STRIP_H;

inline void fillSpan(uint16_t* row, int x0, int x1, uint16_t c) {
    if (x0 < 0) x0 = 0;
    if (x1 > LCD_W) x1 = LCD_W;
    for (int x = x0; x < x1; x++) row[x] = c;
}

inline int floorDiv(int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

inline uint16_t halfBright(uint16_t c) { return (uint16_t)((c >> 1) & 0x7BEF); }

const uint8_t* glyph(char ch) {
    int c = (unsigned char)ch;
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 32 || c > 127) c = '?';
    return kFont5x7[c - 32];
}

}  // namespace

// ------------------------------------------------------------------ Frame

void Frame::clear() {
    count = 0;
    textUsed = 0;
    overflow = false;
    bg = BG_SOLID;
    bgColor = col::BLACK;
    dim = false;
    track = nullptr;
}

void Frame::setSolid(uint16_t c) {
    bg = BG_SOLID;
    bgColor = c;
}

void Frame::setTrack(const Track* t, float s, float x, bool dimmed) {
    bg = BG_TRACK;
    track = t;
    camS = s;
    camX = x;
    dim = dimmed;
}

void Frame::setTest() { bg = BG_TEST; }

DrawItem* Frame::push() {
    if (count >= kMaxItems) {
        overflow = true;
        return nullptr;
    }
    DrawItem* it = &items[count++];
    memset(it, 0, sizeof(*it));
    return it;
}

void Frame::rect(int x, int y, int w, int h, uint16_t c, uint8_t flags) {
    if (w <= 0 || h <= 0) return;
    DrawItem* it = push();
    if (!it) return;
    it->kind = DK_RECT;
    it->flags = flags;
    it->x = (int16_t)x;
    it->y = (int16_t)y;
    it->w = (int16_t)w;
    it->h = (int16_t)h;
    it->c0 = c;
}

void Frame::outline(int x, int y, int w, int h, uint16_t c) {
    rect(x, y, w, 1, c);
    rect(x, y + h - 1, w, 1, c);
    rect(x, y, 1, h, c);
    rect(x + w - 1, y, 1, h, c);
}

void Frame::sprite(uint8_t id, int cx, int cy, uint8_t flags, uint16_t tint) {
    if (id >= SPR_COUNT) return;
    const Sprite& s = kSprites[id];
    int x = cx - s.pivotX, y = cy - s.pivotY;
    if (x >= LCD_W || y >= LCD_H || x + s.w <= 0 || y + s.h <= 0) return;  // fuera de pantalla
    DrawItem* it = push();
    if (!it) return;
    it->kind = DK_SPRITE;
    it->flags = flags;
    it->a = id;
    it->x = (int16_t)x;
    it->y = (int16_t)y;
    it->w = s.w;
    it->h = s.h;
    it->c0 = tint;
}

int Frame::textWidth(const char* s, int scale) {
    int n = (int)strlen(s);
    return n > 0 ? (n * 6 - 1) * scale : 0;
}

int Frame::text(int x, int y, const char* s, uint16_t c, int scale, uint8_t flags) {
    int n = (int)strlen(s);
    if (n == 0) return 0;
    if (textUsed + n + 1 > kTextPool) {
        overflow = true;
        return 0;
    }
    DrawItem* it = push();
    if (!it) return 0;
    memcpy(&textPool[textUsed], s, (size_t)n + 1);
    it->kind = DK_TEXT;
    it->flags = flags;
    it->a = (uint8_t)scale;
    it->x = (int16_t)x;
    it->y = (int16_t)y;
    it->w = (int16_t)textWidth(s, scale);
    it->h = (int16_t)(7 * scale);
    it->c0 = c;
    it->c1 = col::SHADOW;
    it->textOff = (uint16_t)textUsed;
    textUsed += n + 1;
    return it->w;
}

int Frame::textC(int y, const char* s, uint16_t c, int scale, uint8_t flags) {
    return text((LCD_W - textWidth(s, scale)) / 2, y, s, c, scale, flags);
}

void Frame::tri(int cx, int y, int size, bool down, uint16_t c) {
    DrawItem* it = push();
    if (!it) return;
    it->kind = DK_TRI;
    it->flags = down ? DF_DOWN : DF_NONE;
    it->x = (int16_t)cx;
    it->y = (int16_t)y;
    it->h = (int16_t)size;
    it->c0 = c;
}

// ------------------------------------------------------------------ Renderer

void Renderer::start(const Frame& f) {
    f_ = &f;
    active_ = true;
    next_ = 0;
    renderUs_ = 0;
    startUs_ = p_->micros64();
}

void Renderer::service() {
    if (!active_) return;
    while (next_ < kStrips) {
        int slot = next_ & 1;
        if (!p_->lcdSlotFree(slot)) return;
        int y0 = next_ * LCD_STRIP_H;
        int lines = (LCD_H - y0 < LCD_STRIP_H) ? (LCD_H - y0) : LCD_STRIP_H;
        int64_t t0 = p_->micros64();
        renderStrip(p_->lcdBuffer(slot), y0, lines);
        renderUs_ += p_->micros64() - t0;
        p_->lcdPush(slot, y0, lines);
        next_++;
    }
    if (!p_->lcdIdle()) return;
    int64_t now = p_->micros64();
    active_ = false;
    frames++;
    lastFrameUs = (uint32_t)(now - startUs_);
    lastRenderUs = (uint32_t)renderUs_;
    fpsFrames_++;
    if (fpsWindowUs_ == 0) fpsWindowUs_ = now;
    if (now - fpsWindowUs_ >= 1000000) {
        fps = (float)fpsFrames_ * 1e6f / (float)(now - fpsWindowUs_);
        fpsFrames_ = 0;
        fpsWindowUs_ = now;
    }
}

void Renderer::backgroundRow(uint16_t* row, int y) {
    const Frame& f = *f_;
    if (f.bg == BG_SOLID || (f.bg == BG_TRACK && !f.track)) {
        fillSpan(row, 0, LCD_W, f.bgColor);
        return;
    }
    if (f.bg == BG_TEST) {
        // barras de color en la mitad superior, degrade en la inferior
        if (y < LCD_H / 2) {
            static const uint16_t bars[] = {col::RED, col::GREEN, col::BLUE, col::WHITE};
            int band = y * 4 / (LCD_H / 2);
            fillSpan(row, 0, LCD_W, bars[band]);
        } else {
            for (int x = 0; x < LCD_W; x++) {
                int v = x * 255 / (LCD_W - 1);
                row[x] = (y < LCD_H * 3 / 4) ? rgb565((uint8_t)v, (uint8_t)v, (uint8_t)v)
                                             : rgb565((uint8_t)v, 0, (uint8_t)(255 - v));
            }
        }
        return;
    }

    const Track& t = *f.track;
    float s = f.camS + (float)(CAM_CAR_Y - y);
    int si = (int)floorf(s);
    float base = t.center(s) - f.camX + (float)(LCD_W / 2);
    int roadL = (int)lrintf(base - Track::kHalfWidth);
    int roadR = (int)lrintf(base + Track::kHalfWidth);
    int curb = (int)Track::kCurbW;
    float rail = t.railInner();
    int railL1 = (int)lrintf(base - rail), railL0 = railL1 - (int)Track::kRailW;
    int railR0 = (int)lrintf(base + rail), railR1 = railR0 + (int)Track::kRailW;

    uint16_t grass = (floorDiv(si, 16) & 1) ? col::GRASS_A : col::GRASS_B;
    uint16_t curbC = (floorDiv(si, 8) & 1) ? col::CURB_RED : col::CURB_WHITE;
    uint16_t asphalt = (floorDiv(si, 32) & 1) ? col::ASPHALT_A : col::ASPHALT_B;
    uint16_t railC = (floorDiv(si, 4) % 3 == 0) ? col::RAIL_POST : col::RAIL;

    fillSpan(row, 0, railL0, grass);
    fillSpan(row, railL0, railL1, railC);
    fillSpan(row, railL1, roadL, grass);
    fillSpan(row, roadL, roadL + curb, curbC);
    float ws = t.wrap(s);
    if (ws < Track::kFinishDepth) {
        int cell = floorDiv(si, 6);
        for (int x = roadL + curb; x < roadR - curb; x++) {
            if (x < 0 || x >= LCD_W) continue;
            row[x] = (((x - roadL) / 6 + cell) & 1) ? col::WHITE : col::BLACK;
        }
    } else {
        fillSpan(row, roadL + curb, roadR - curb, asphalt);
        if ((si & 31) < 16) {
            int cx = (int)lrintf(base);
            fillSpan(row, cx - 1, cx + 1, col::LANE);
        }
    }
    fillSpan(row, roadR - curb, roadR, curbC);
    fillSpan(row, roadR, railR0, grass);
    fillSpan(row, railR0, railR1, railC);
    fillSpan(row, railR1, LCD_W, grass);

    if (f.dim) {
        for (int x = 0; x < LCD_W; x++) row[x] = halfBright(row[x]);
    }
}

void Renderer::renderStrip(uint16_t* buf, int y0, int lines) {
    const Frame& f = *f_;
    for (int r = 0; r < lines; r++) backgroundRow(buf + r * LCD_W, y0 + r);
    int y1 = y0 + lines;

    for (int k = 0; k < f.count; k++) {
        const DrawItem& it = f.items[k];
        switch (it.kind) {
            case DK_RECT: {
                int ya = it.y > y0 ? it.y : y0;
                int yb = (it.y + it.h) < y1 ? (it.y + it.h) : y1;
                int xa = it.x > 0 ? it.x : 0;
                int xb = (it.x + it.w) < LCD_W ? (it.x + it.w) : LCD_W;
                for (int y = ya; y < yb; y++) {
                    uint16_t* row = buf + (y - y0) * LCD_W;
                    if (it.flags & DF_DITHER) {
                        for (int x = xa + ((xa + y) & 1); x < xb; x += 2) row[x] = it.c0;
                    } else {
                        for (int x = xa; x < xb; x++) row[x] = it.c0;
                    }
                }
                break;
            }
            case DK_SPRITE: {
                const Sprite& s = kSprites[it.a];
                int stride = (s.w + 7) >> 3;
                int ya = it.y > y0 ? it.y : y0;
                int yb = (it.y + s.h) < y1 ? (it.y + s.h) : y1;
                for (int y = ya; y < yb; y++) {
                    int j = y - it.y;
                    const uint8_t* m = s.mask + j * stride;
                    const uint16_t* px = s.pixels + j * s.w;
                    uint16_t* row = buf + (y - y0) * LCD_W;
                    for (int i = 0; i < s.w; i++) {
                        int x = it.x + i;
                        if (x < 0 || x >= LCD_W) continue;
                        if (!(m[i >> 3] & (0x80 >> (i & 7)))) continue;
                        if ((it.flags & DF_DITHER) && ((x + y) & 1)) continue;
                        row[x] = (it.flags & DF_SILHOUETTE) ? it.c0 : px[i];
                    }
                }
                break;
            }
            case DK_TEXT: {
                int sc = it.a;
                const char* str = &f.textPool[it.textOff];
                for (int pass = (it.flags & DF_SHADOW) ? 0 : 1; pass < 2; pass++) {
                    int off = (pass == 0) ? (sc > 1 ? sc / 2 + 1 : 1) : 0;
                    uint16_t c = (pass == 0) ? it.c1 : it.c0;
                    int gy = it.y + off;
                    if (gy >= y1 || gy + 7 * sc <= y0) continue;
                    int gx = it.x + off;
                    for (const char* p = str; *p; p++, gx += 6 * sc) {
                        if (gx >= LCD_W || gx + 5 * sc <= 0) continue;
                        const uint8_t* g = glyph(*p);
                        for (int gr = 0; gr < 7; gr++) {
                            int ry0 = gy + gr * sc;
                            int ry1 = ry0 + sc;
                            if (ry1 <= y0 || ry0 >= y1) continue;
                            uint8_t bits = g[gr];
                            if (!bits) continue;
                            if (ry0 < y0) ry0 = y0;
                            if (ry1 > y1) ry1 = y1;
                            for (int gc = 0; gc < 5; gc++) {
                                if (!(bits & (0x10 >> gc))) continue;
                                int rx0 = gx + gc * sc, rx1 = rx0 + sc;
                                if (rx0 < 0) rx0 = 0;
                                if (rx1 > LCD_W) rx1 = LCD_W;
                                for (int y = ry0; y < ry1; y++) {
                                    uint16_t* row = buf + (y - y0) * LCD_W;
                                    for (int x = rx0; x < rx1; x++) row[x] = c;
                                }
                            }
                        }
                    }
                }
                break;
            }
            case DK_TRI: {
                bool down = it.flags & DF_DOWN;
                for (int k2 = 0; k2 < it.h; k2++) {
                    int y = it.y + k2;
                    if (y < y0 || y >= y1) continue;
                    int half = down ? (it.h - 1 - k2) : k2;
                    fillSpan(buf + (y - y0) * LCD_W, it.x - half, it.x + half + 1, it.c0);
                }
                break;
            }
        }
    }
}
