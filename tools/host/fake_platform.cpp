#include "fake_platform.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

int64_t g_nowUs = 0;

namespace {
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint32_t lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}
}  // namespace

// ------------------------------------------------------------------ Medium

void Medium::send(FakeBoard* from, const uint8_t* mac, const void* data, size_t len) {
    sent++;
    for (FakeBoard* b : boards) {
        if (b == from || !b->online) continue;
        if (memcmp(mac, kBroadcast, 6) != 0 && memcmp(mac, b->mac, 6) != 0) continue;
        if (blackout || (double)(lcg(rng_) >> 8) / 16777216.0 < loss) {
            dropped++;
            continue;
        }
        InFlight f;
        f.to = b;
        memcpy(f.pkt.mac, from->mac, 6);
        f.pkt.len = (uint8_t)len;
        f.pkt.rssi = -45;
        memcpy(f.pkt.data, data, len);
        f.deliverUs = g_nowUs + latMinUs + (int64_t)(lcg(rng_) % (uint32_t)(latMaxUs - latMinUs + 1));
        q_.push_back(f);
    }
}

void Medium::deliver() {
    for (auto it = q_.begin(); it != q_.end();) {
        if (it->deliverUs <= g_nowUs) {
            it->pkt.rxMs = it->to->millis();
            if (it->to->online) it->to->deliverRx(it->pkt);
            it = q_.erase(it);
        } else {
            ++it;
        }
    }
}

// ------------------------------------------------------------------ FakeBoard

FakeBoard::FakeBoard(const char* n, const uint8_t m[6], Medium* medium) : name(n), m_(medium) {
    memcpy(mac, m, 6);
    rng_ = (uint32_t)m[5] * 7919u + (uint32_t)m[4] * 31u + 17u;
    memset(screen, 0, sizeof(screen));
    medium->boards.push_back(this);
}

uint32_t FakeBoard::random32() { return lcg(rng_); }

void FakeBoard::netMyMac(uint8_t out[6]) { memcpy(out, mac, 6); }

bool FakeBoard::netSetPeer(const uint8_t m[6]) {
    memcpy(peer_, m, 6);
    hasPeer_ = true;
    return true;
}

bool FakeBoard::netSend(const uint8_t* m, const void* data, size_t len) {
    if (!online) return false;
    const uint8_t* dst = m ? m : kBroadcast;
    // como ESP-NOW real: unicast solo a un par registrado
    if (memcmp(dst, kBroadcast, 6) != 0 && !(hasPeer_ && memcmp(dst, peer_, 6) == 0)) {
        counters_.txFail++;
        return false;
    }
    m_->send(this, dst, data, len);
    counters_.txOk++;
    return true;
}

void FakeBoard::deliverRx(const RxPacket& p) {
    if (rx_.size() >= 16) {
        counters_.rxDropped++;
        return;
    }
    rx_.push_back(p);
    counters_.rx++;
}

bool FakeBoard::netReceive(RxPacket& out) {
    if (rx_.empty()) return false;
    out = rx_.front();
    rx_.pop_front();
    return true;
}

bool FakeBoard::imuLatest(ImuSample& out) {
    // Postura "volante": pantalla vertical mirando al jugador. Inclinar a la derecha rota
    // alrededor de la normal de la pantalla. Fuerza especifica en ejes de pantalla
    // (X derecha, Y arriba, Z hacia el jugador): (-sin t, cos t, 0).
    // El chip esta rotado: chipX = pantallaY, chipY = -pantallaX, chipZ = pantallaZ,
    // para que la deteccion de eje no pueda acertar de casualidad con X.
    float t = tiltDeg * 3.14159265f / 180.0f;
    float sx = -sinf(t), sy = cosf(t), sz = 0.0f;
    auto n = [&]() { return ((float)(lcg(rng_) >> 8) / 16777216.0f * 2.0f - 1.0f) * noiseG; };
    imuCount_ = (uint32_t)(micros64() / 10000);  // 100 Hz
    out.ax = sy + n();
    out.ay = -sx + n();
    out.az = sz + n();
    out.gx = out.gy = out.gz = 0;
    out.tempC = 31.5f;
    out.tMs = millis();
    out.count = imuCount_;
    return true;
}

bool FakeBoard::buttonDown() {
    uint32_t now = millis();
    for (const Press& p : presses_)
        if (now >= p.at && now < p.at + p.dur) return true;
    return false;
}

void FakeBoard::lcdPush(int slot, int y0, int lines) {
    memcpy(&screen[y0 * LCD_W], strip_[slot], (size_t)lines * LCD_W * 2);
    // demora del SPI: las tiras salen una detras de otra
    int64_t bytes = (int64_t)lines * LCD_W * 2;
    int64_t us = bytes * 8 * 1000000 / LCD_SPI_HZ + 30;
    int64_t start = spiFreeAt_ > micros64() ? spiFreeAt_ : micros64();
    spiFreeAt_ = start + us;
    busyUntil_[slot] = spiFreeAt_;
    if (y0 + lines >= LCD_H) framesPushed++;
}

bool FakeBoard::loadSettings(Settings& s) {
    if (!hasSettings_) return false;
    s = settings_;
    return true;
}

void FakeBoard::log(const char* msg) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%8.3f %-7s %s", g_nowUs / 1e6, name.c_str(), msg);
    logs.push_back(buf);
    if (!quietLogs) printf("%s\n", buf);
}

void FakeBoard::savePpm(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", LCD_W, LCD_H);
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        uint16_t c = screen[i];
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63),
                          (uint8_t)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
