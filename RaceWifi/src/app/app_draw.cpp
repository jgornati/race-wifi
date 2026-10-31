// Composicion de cada pantalla en la lista de dibujo (Frame). No toca el hardware.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"

namespace {

void fmtTime(int32_t ms, char* out, size_t n) {
    if (ms < 0) ms = 0;
    int t = ms / 100;
    snprintf(out, n, "%d:%02d.%d", t / 600, (t / 10) % 60, t % 10);
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

void App::compose(uint32_t now) {
    frame_.clear();
    switch (screen_) {
        case Screen::Boot: composeBoot(now); break;
        case Screen::Search: composeSearch(now); break;
        case Screen::Calibrate: composeCalibrate(now); break;
        case Screen::AxisSetup: composeAxis(now); break;
        case Screen::Game: composeGame(now); break;
        case Screen::Diag: composeDiag(now); break;
        case Screen::Sleep: composeSleep(now); break;
    }
}

void App::panel(int y, int h) {
    frame_.rect(8, y, LCD_W - 16, h, col::PANEL);
    frame_.outline(8, y, LCD_W - 16, h, col::GREY);
}

bool App::worldToScreen(float s, float x, int& sx, int& sy) const {
    sy = CAM_CAR_Y - (int)lrintf(s - frame_.camS);
    sx = (int)lrintf(x - frame_.camX) + LCD_W / 2;
    return sy > -40 && sy < LCD_H + 40 && sx > -40 && sx < LCD_W + 40;
}

// ------------------------------------------------------------------ menus

void App::composeBoot(uint32_t now) {
    (void)now;
    frame_.setSolid(col::DARK);
    frame_.textC(60, "RACE", col::RED, 4);
    frame_.textC(100, "WIFI", col::BLUE, 4);
    frame_.sprite(SPR_CAR_RED, LCD_W / 2 - 22, 175);
    frame_.sprite(SPR_CAR_BLUE, LCD_W / 2 + 22, 175);
    frame_.textC(220, "ESP32-S3-LCD-1.47B", col::GREY);
    char buf[32];
    snprintf(buf, sizeof(buf), "V%s", FW_VERSION);
    frame_.textC(232, buf, col::GREY);
    frame_.textC(260, p_->imuPresent() ? "SENSOR OK" : "SENSOR NO DETECTADO", p_->imuPresent() ? col::GREEN : col::RED);
    frame_.textC(272, netOk_ ? "ESP-NOW OK" : "ESP-NOW ERROR", netOk_ ? col::GREEN : col::RED);
}

void App::composeSearch(uint32_t now) {
    float camS = (float)(now % 600000u) * 0.12f;  // pista de fondo avanzando sola
    frame_.setTrack(&track_, camS, track_.center(camS + CAM_LOOKAHEAD), true);
    CarDraw none;
    drawWorldObjects(none, none, now);

    panel(56, 128);
    frame_.textC(68, "BUSCANDO", col::YELLOW, 2);
    static const char* dots[] = {"RIVAL", "RIVAL.", "RIVAL..", "RIVAL..."};
    frame_.text(LCD_W / 2 - 29, 90, dots[(now / 400) % 4], col::YELLOW, 2);
    char buf[40];
    if (!netOk_) {
        frame_.textC(118, "ERROR ESP-NOW", col::RED);
    } else if (link_.state == LinkState::Pairing) {
        frame_.textC(118, "EMPAREJANDO...", col::GREEN);
    } else {
        snprintf(buf, sizeof(buf), "CANAL %d", p_->netChannel());
        frame_.textC(118, buf, col::WHITE);
    }
    snprintf(buf, sizeof(buf), "MI MAC ..%02X:%02X:%02X", link_.myMac[3], link_.myMac[4], link_.myMac[5]);
    frame_.textC(132, buf, col::GREY);
    frame_.textC(150, "MISMO FIRMWARE EN", col::GREY);
    frame_.textC(162, "LAS DOS PLACAS", col::GREY);
    if (!p_->imuPresent()) frame_.textC(196, "SIN SENSOR!", col::RED, 2);

    panel(220, 92);
    frame_.textC(228, "BOOT CORTO:", col::WHITE);
    frame_.textC(240, "PRACTICA SOLO", col::YELLOW);
    frame_.textC(256, "BOOT LARGO:", col::WHITE);
    frame_.textC(268, "DIAGNOSTICO", col::YELLOW);
    frame_.textC(284, "BOOT 3 SEGUNDOS:", col::WHITE);
    frame_.textC(296, "APAGAR", col::YELLOW);
}

void App::composeSleep(uint32_t now) {
    frame_.setSolid(col::DARK);
    frame_.textC(130, "CHAU", col::WHITE, 4);
    frame_.textC(180, "BOOT PARA PRENDER", col::GREY);
}

void App::composeCalibrate(uint32_t now) {
    frame_.setSolid(col::DARK);
    frame_.textC(40, "CALIBRANDO", col::YELLOW, 2);
    frame_.textC(76, "SOSTENE LA PLACA", col::WHITE);
    frame_.textC(88, "COMO PARA MANEJAR", col::WHITE);
    frame_.textC(100, "Y DEJALA QUIETA", col::WHITE);
    frame_.sprite(SPR_CAR_RED, LCD_W / 2, 150);
    float p = calib_.progress(now);
    frame_.outline(20, 190, LCD_W - 40, 12, col::GREY);
    frame_.rect(22, 192, (int)((LCD_W - 44) * p), 8, col::GREEN);
    if (!imuValid_) {
        frame_.textC(220, "SIN DATOS DEL SENSOR", col::RED);
    } else if (calib_.movedRecently(now)) {
        frame_.textC(220, "SE MOVIO: REINTENTO", col::ORANGE);
    }
    frame_.textC(296, "MUY LARGO: SALIR", col::GREY);
}

void App::composeAxis(uint32_t now) {
    frame_.setSolid(col::DARK);
    frame_.textC(30, "CONFIGURAR", col::YELLOW, 2);
    frame_.textC(50, "GIRO", col::YELLOW, 2);
    AxisWizard::Status st = wizard_.status();
    if (st == AxisWizard::WaitReturn || st == AxisWizard::Done) {
        char buf[32];
        snprintf(buf, sizeof(buf), "EJE %c%c DETECTADO", "XYZ"[wizard_.axis()], wizard_.sign() > 0 ? '+' : '-');
        frame_.textC(100, buf, col::GREEN);
        frame_.textC(130, "VOLVE AL CENTRO", col::WHITE, 1);
        frame_.sprite(SPR_CAR_RED, LCD_W / 2, 190);
    } else {
        frame_.textC(90, "INCLINA LA PLACA", col::WHITE);
        frame_.textC(102, "A LA DERECHA", col::WHITE);
        frame_.textC(114, "Y MANTENELA", col::WHITE);
        frame_.text(LCD_W / 2 - 50, 160, ">>>", col::YELLOW, 4);
        frame_.sprite(SPR_CAR_RED_RIGHT, LCD_W / 2 + 40, 220);
        float p = wizard_.holdProgress(now);
        frame_.outline(20, 250, LCD_W - 40, 12, col::GREY);
        frame_.rect(22, 252, (int)((LCD_W - 44) * p), 8, col::GREEN);
    }
    frame_.textC(296, "MUY LARGO: SALIR", col::GREY);
}

// ------------------------------------------------------------------ carrera

void App::drawCar(const Car& c, int colorIdx, bool ghost, uint32_t now) {
    int sx, sy;
    if (!worldToScreen(c.s, c.x, sx, sy)) return;
    uint8_t base = colorIdx == 0 ? SPR_CAR_RED : SPR_CAR_BLUE;
    uint8_t spr = base;
    if (carBraking(c) || c.v < 1.0f)
        spr = base + 3;  // luces traseras
    else if (c.steer < -0.3f)
        spr = base + 1;
    else if (c.steer > 0.3f)
        spr = base + 2;
    uint8_t flags = ghost ? DF_DITHER : DF_NONE;
    if (c.invulnMs > 0 && ((now / 80) & 1)) flags = DF_DITHER;  // parpadeo tras chocar
    frame_.sprite(spr, sx + 2, sy + 3, DF_SILHOUETTE | flags, col::CAR_SHADOW);
    frame_.sprite(spr, sx, sy, flags);
}

void App::drawWorldObjects(const CarDraw& me, const CarDraw& rival, uint32_t now) {
    float camS = frame_.camS;
    float sMin = camS - (float)(LCD_H - CAM_CAR_Y) - 40.0f;
    float sMax = camS + (float)CAM_CAR_Y + 40.0f;
    int kc = (int)floorf(camS / Track::kLength);
    int sx, sy;
    for (int k = kc - 1; k <= kc + 1; k++) {
        float base = (float)k * Track::kLength;
        for (int i = 0; i < track_.decorCount(); i++) {
            const Decor& d = track_.decor(i);
            float s = base + d.s;
            if (s < sMin || s > sMax) continue;
            if (worldToScreen(s, track_.center(s) + d.off, sx, sy)) frame_.sprite(d.sprite, sx, sy);
        }
        for (int i = 0; i < track_.obstacleCount(); i++) {
            const Obstacle& o = track_.obstacle(i);
            float s = base + o.s;
            if (s < sMin || s > sMax) continue;
            if (worldToScreen(s, track_.center(s) + o.off, sx, sy))
                frame_.sprite(o.type == OBS_CONE ? SPR_CONE : SPR_TIRES, sx, sy);
        }
    }
    for (const Particle& p : fx_.p) {
        if (p.alive && p.kind == FX_SMOKE && worldToScreen(p.s, p.x, sx, sy)) frame_.sprite(fx_.spriteFor(p), sx, sy);
    }
    int myIdx = myIndex();
    if (rival.visible) drawCar(rival.c, 1 - myIdx, rival.ghost, now);
    if (me.visible) drawCar(me.c, myIdx, false, now);
    for (const Particle& p : fx_.p) {
        if (p.alive && p.kind == FX_SPARK && worldToScreen(p.s, p.x, sx, sy)) frame_.sprite(SPR_SPARK, sx, sy);
    }
}

void App::drawHud(const CarDraw& me, const CarDraw& rival, uint32_t now) {
    char buf[24];
    frame_.rect(0, 0, LCD_W, HUD_H, col::PANEL);
    if (!solo_) {
        int pos = myPosition();
        snprintf(buf, sizeof(buf), "%d/2", pos);
        frame_.text(3, 3, buf, pos == 1 ? col::YELLOW : col::WHITE, 1, DF_NONE);
    }
    int lap = (link_.active() && link_.role == Role::Client)
                  ? (cv_.haveState ? cv_.st.cars[1].lap : 1)
                  : carLap(race_.cars[myIndex()]);
    if (lap < 1) lap = 1;
    if (lap > lastLap_) {
        lastLap_ = lap;
        lapBannerUntil_ = now + 1500;
    }
    snprintf(buf, sizeof(buf), "V%d/%d", lap, RACE_LAPS);
    frame_.textC(3, buf, col::WHITE, 1, DF_NONE);
    fmtTime(raceTimeMs(now), buf, sizeof(buf));
    frame_.text(LCD_W - 3 - Frame::textWidth(buf, 1), 3, buf, col::WHITE, 1, DF_NONE);
    if (link_.stale(now) && link_.state == LinkState::Connected) frame_.text(44, 3, "!", col::ORANGE, 1, DF_NONE);

    // flecha y distancia al rival cuando no esta en pantalla
    if (rival.visible && me.visible) {
        int sx, sy;
        worldToScreen(rival.c.s, rival.c.x, sx, sy);
        bool above = sy < HUD_H + 4;
        bool below = sy > LCD_H + 16;
        if (above || below) {
            int ax = clampi(sx, 10, LCD_W - 10);
            int dist = (int)lrintf(fabsf(rival.c.s - me.c.s) / PX_PER_METER);
            snprintf(buf, sizeof(buf), "%dM", dist);
            int w = Frame::textWidth(buf, 1);
            int tx = clampi(ax - w / 2, 1, LCD_W - w - 1);
            uint16_t rc = myIndex() == 0 ? col::BLUE : col::RED;
            if (above) {
                frame_.tri(ax, HUD_H + 3, 7, false, rc);
                frame_.text(tx, HUD_H + 12, buf, col::WHITE);
            } else {
                frame_.tri(ax, LCD_H - 9, 7, true, rc);
                frame_.text(tx, LCD_H - 20, buf, col::WHITE);
            }
        }
    }
}

void App::composeGame(uint32_t now) {
    CarDraw me;
    me.visible = true;
    me.c = myCar();
    CarDraw rv;
    rivalCar(now, rv);
    Phase ph = phase();
    bool client = link_.active() && link_.role == Role::Client;
    bool paused = client ? (cv_.haveState && (cv_.st.flags & ST_PAUSED)) : race_.paused;

    frame_.setTrack(&track_, me.c.s, camX_, ph == Phase::Lobby || ph == Phase::Results);
    drawWorldObjects(me, rv, now);
    drawHud(me, rv, now);

    char buf[32];
    int myIdx = myIndex();
    switch (ph) {
        case Phase::Lobby: {
            uint8_t mask = client ? (cv_.haveState ? cv_.st.readyMask : 0) : race_.readyMask;
            panel(60, 170);
            frame_.textC(72, solo_ ? "PRACTICA" : "LISTOS?", col::YELLOW, 2);
            int rows = solo_ ? 1 : 2;
            for (int i = 0; i < rows; i++) {
                int y = 112 + i * 40;
                frame_.sprite(i == 0 ? SPR_CAR_RED : SPR_CAR_BLUE, 34, y);
                bool rdy = (i == myIdx) ? localReady_ : (mask & (1 << i));
                if (solo_) {
                    frame_.text(54, y - 3, "TU AUTO", col::WHITE);
                } else {
                    frame_.text(54, y - 9, i == myIdx ? "TU" : "RIVAL", col::GREY);
                    frame_.text(54, y + 2, rdy ? "LISTO" : "ESPERANDO", rdy ? col::GREEN : col::WHITE);
                }
            }
            frame_.textC(190, solo_ ? "BOOT: LARGAR" : (localReady_ ? "BOOT: CANCELAR" : "BOOT: LISTO"), col::YELLOW);
            frame_.textC(204, "LARGO: CALIBRAR", col::GREY);
            frame_.textC(216, "MUY LARGO: SALIR", col::GREY);
            break;
        }
        case Phase::Countdown: {
            uint32_t startHost = client ? cv_.st.startHostMs : race_.startHostMs;
            uint32_t hnow = client ? link_.hostNow(now) : now;
            int32_t rem = (int32_t)(startHost - hnow);
            if (rem <= 0 || (client && cv_.localRacing)) {
                frame_.textC(110, "YA!", col::GREEN, 6);
            } else {
                int n = clampi((rem + 999) / 1000, 1, 3);
                snprintf(buf, sizeof(buf), "%d", n);
                static const uint16_t colors[] = {col::YELLOW, col::ORANGE, col::RED};
                frame_.textC(100, buf, colors[n - 1], 8);
                if (raceTimeMs(now) > 0) frame_.textC(76, "SIGUE LA CARRERA", col::WHITE);
            }
            break;
        }
        case Phase::Race: {
            int32_t t = raceTimeMs(now);
            if (paused) {
                frame_.textC(110, "PAUSA", col::ORANGE, 3);
            } else if (t < 800) {
                frame_.textC(110, "YA!", col::GREEN, 6);
            } else if (now < lapBannerUntil_ && lastLap_ > 1) {
                if (lastLap_ == RACE_LAPS)
                    frame_.textC(60, "ULTIMA VUELTA", col::YELLOW, 2);
                else {
                    snprintf(buf, sizeof(buf), "VUELTA %d", lastLap_);
                    frame_.textC(60, buf, col::WHITE, 2);
                }
            }
            if (me.c.finished) {
                frame_.textC(70, "LLEGASTE!", col::YELLOW, 2);
                if (!solo_) frame_.textC(92, "ESPERANDO AL RIVAL", col::WHITE);
            }
            break;
        }
        case Phase::Results: {
            uint8_t w = winner();
            panel(50, 210);
            if (solo_)
                frame_.textC(62, "LLEGADA", col::YELLOW, 3);
            else if (w == WINNER_TIE)
                frame_.textC(62, "EMPATE", col::WHITE, 3);
            else if (w == myIdx)
                frame_.textC(62, "GANASTE!", col::YELLOW, 3);
            else
                frame_.textC(62, "SEGUNDO", col::WHITE, 3);
            int rows = solo_ ? 1 : 2;
            for (int i = 0; i < rows; i++) {
                int y = 112 + i * 40;
                bool fin;
                uint32_t fms;
                if (client) {
                    fin = cv_.st.cars[i].flags & CF_FINISHED;
                    fms = cv_.st.cars[i].finishMs;
                } else {
                    fin = race_.cars[i].finished;
                    fms = race_.cars[i].finishMs;
                }
                frame_.sprite(i == 0 ? SPR_CAR_RED : SPR_CAR_BLUE, 34, y);
                if (fin)
                    fmtTime((int32_t)fms, buf, sizeof(buf));
                else
                    snprintf(buf, sizeof(buf), "NO LLEGO");
                frame_.text(54, y - 9, i == myIdx ? "TU" : "RIVAL", col::GREY);
                frame_.text(54, y + 2, buf, (w == i) ? col::YELLOW : col::WHITE, 2);
            }
            uint8_t rm = client ? cv_.st.rematchMask : race_.rematchMask;
            bool rivalWants = !solo_ && (rm & (1 << (1 - myIdx)));
            if (localRematch_)
                frame_.textC(200, "ESPERANDO AL RIVAL", col::GREEN);
            else
                frame_.textC(200, solo_ ? "BOOT: OTRA VEZ" : "BOOT: REVANCHA", col::YELLOW);
            if (rivalWants) frame_.textC(214, "EL RIVAL QUIERE REVANCHA", col::ORANGE);
            frame_.textC(232, "LARGO: CALIBRAR", col::GREY);
            frame_.textC(244, "MUY LARGO: SALIR", col::GREY);
            break;
        }
    }
    composeLinkOverlay(now);
}

void App::composeLinkOverlay(uint32_t now) {
    if (solo_ || link_.state != LinkState::Lost) return;
    panel(110, 100);
    frame_.textC(122, link_.peerLeftFlag ? "EL RIVAL SALIO" : "SIN CONEXION", col::RED, link_.peerLeftFlag ? 1 : 2);
    char buf[32];
    if (!link_.peerLeftFlag) {
        snprintf(buf, sizeof(buf), "ESPERANDO... %lus", (unsigned long)((now - link_.lastRxMs) / 1000));
        frame_.textC(150, buf, col::WHITE);
    }
    frame_.textC(172, "BOOT: BUSCAR RIVAL", col::YELLOW);
    frame_.textC(186, "(VOLVER A EMPAREJAR)", col::GREY);
}
