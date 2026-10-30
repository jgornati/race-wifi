// Modo diagnostico: pantalla, ejes del sensor, direccion, calibracion, rendimiento y red.
// Corto: pagina siguiente. Largo: accion de la pagina. Muy largo: salir.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"

namespace {

const char* kTitles[] = {"PANTALLA", "SENSOR", "DIRECCION", "CALIBRACION", "RENDIMIENTO", "RED"};
const char* kActions[] = {nullptr, nullptr, "LARGO: SENSIBILIDAD", "LARGO: CALIBRAR+EJE", nullptr,
                          "LARGO: BUSCAR/PARAR"};

const char* linkName(LinkState s) {
    switch (s) {
        case LinkState::Off: return "APAGADO";
        case LinkState::Searching: return "BUSCANDO";
        case LinkState::Pairing: return "EMPAREJANDO";
        case LinkState::Connected: return "CONECTADO";
        case LinkState::Lost: return "PERDIDO";
    }
    return "?";
}

}  // namespace

void App::composeDiag(uint32_t now) {
    char buf[48];
    int page = diagPage_;

    if (page == 0) {
        frame_.setTest();
        // el borde amarillo de 1 px tiene que verse completo en los 4 lados (offsets)
        frame_.outline(0, 0, LCD_W, LCD_H, col::YELLOW);
        frame_.textC(15, "^ ARRIBA ^", col::WHITE);
        frame_.textC(25, "ROJO", col::WHITE, 2);
        frame_.textC(62, "VERDE", col::WHITE, 2);
        frame_.textC(102, "AZUL", col::WHITE, 2);
        frame_.textC(142, "BLANCO", col::BLACK, 2, DF_NONE);
        frame_.textC(172, "172x320 COL+34", col::WHITE);
        snprintf(buf, sizeof(buf), "FPS %.1f", renderer_.fps);
        frame_.textC(186, buf, col::WHITE);
        frame_.text(4, 250, "<IZQ", col::WHITE);
        frame_.text(LCD_W - 4 - Frame::textWidth("DER>", 1), 250, "DER>", col::WHITE);
    } else {
        frame_.setSolid(col::DARK);
    }

    // encabezado y pie comunes
    frame_.rect(1, 1, LCD_W - 2, 11, col::PANEL);
    snprintf(buf, sizeof(buf), "DIAG %d/6 %s", page + 1, kTitles[page]);
    frame_.text(3, 3, buf, col::YELLOW, 1, DF_NONE);
    frame_.rect(1, LCD_H - 37, LCD_W - 2, 36, col::PANEL);
    frame_.text(3, LCD_H - 34, "CORTO: SIGUIENTE", col::GREY, 1, DF_NONE);
    if (kActions[page]) frame_.text(3, LCD_H - 23, kActions[page], col::GREEN, 1, DF_NONE);
    frame_.text(3, LCD_H - 12, "MUY LARGO: SALIR", col::GREY, 1, DF_NONE);
    if (page == 0) return;

    int y = 20;
    auto line = [&](const char* s, uint16_t c) {
        frame_.text(4, y, s, c, 1, DF_NONE);
        y += 11;
    };

    // tasa real del sensor
    if (now - imuRateMs_ >= 1000) {
        imuHz_ = (float)(imu_.count - imuRateCount_) * 1000.0f / (float)(now - imuRateMs_);
        imuRateCount_ = imu_.count;
        imuRateMs_ = now;
    }

    switch (page) {
        case 1: {  // SENSOR
            if (p_->imuPresent()) {
                snprintf(buf, sizeof(buf), "QMI8658 I2C 0x%02X ID 0x%02X", p_->imuAddress(), p_->imuWhoAmI());
                line(buf, col::GREEN);
            } else {
                line("QMI8658 NO DETECTADO", col::RED);
            }
            snprintf(buf, sizeof(buf), "LECTURAS %.0f HZ", imuHz_);
            line(buf, col::WHITE);
            y += 4;
            line("ACELERACION (G)", col::YELLOW);
            const float a[3] = {imu_.ax, imu_.ay, imu_.az};
            for (int i = 0; i < 3; i++) {
                bool steerAxis = axisKnown_ && i == tilt_.params.axis;
                snprintf(buf, sizeof(buf), "%c %+.3f%s", "XYZ"[i], a[i], steerAxis ? " GIRO" : "");
                frame_.text(4, y, buf, steerAxis ? col::GREEN : col::WHITE, 1, DF_NONE);
                int cx = 128, len = (int)lrintf(a[i] * 36.0f);
                if (len > 40) len = 40;
                if (len < -40) len = -40;
                frame_.rect(cx - 40, y + 2, 81, 3, col::PANEL);
                frame_.rect(len >= 0 ? cx : cx + len, y + 1, len >= 0 ? len + 1 : -len, 5, col::GREEN);
                frame_.rect(cx, y, 1, 7, col::GREY);
                y += 12;
            }
            y += 4;
            line("GIROSCOPO (GRADOS/S)", col::YELLOW);
            snprintf(buf, sizeof(buf), "X %+.1f", imu_.gx);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "Y %+.1f", imu_.gy);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "Z %+.1f", imu_.gz);
            line(buf, col::WHITE);
            y += 4;
            snprintf(buf, sizeof(buf), "TEMPERATURA %.1f C", imu_.tempC);
            line(buf, col::WHITE);
            float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
            snprintf(buf, sizeof(buf), "|A| %.3f G (QUIETA = 1)", mag);
            line(buf, col::WHITE);
            y += 6;
            line("INCLINA Y MIRA QUE EJE", col::GREY);
            line("CAMBIA: ESE ES EL DE GIRO", col::GREY);
            break;
        }
        case 2: {  // DIRECCION
            if (!calibrated_) {
                line("SIN CALIBRAR:", col::ORANGE);
                line("USA LA PAGINA 4", col::ORANGE);
                break;
            }
            snprintf(buf, sizeof(buf), "ANGULO %+.1f\x7f (CRUDO %+.1f)", tilt_.angleDeg(), tilt_.rawDeg());
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "GIRO %+.2f", tilt_.steer());
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "ZONA MUERTA %.0f\x7f", tilt_.params.deadzoneDeg);
            line(buf, col::GREY);
            snprintf(buf, sizeof(buf), "GIRO MAXIMO %.0f\x7f", tilt_.params.maxDeg);
            line(buf, col::GREEN);
            snprintf(buf, sizeof(buf), "FILTRO %.0f MS", tilt_.params.filterMs);
            line(buf, col::GREY);
            // barra: centro = neutro, marcas = zona muerta y maximo
            int cx = LCD_W / 2, half = 70, by = y + 10;
            frame_.rect(cx - half, by, 2 * half + 1, 10, col::PANEL);
            int dz = (int)lrintf(half * tilt_.params.deadzoneDeg / tilt_.params.maxDeg);
            frame_.rect(cx - dz, by, 2 * dz + 1, 10, col::GREY);
            float ang = tilt_.angleDeg() / tilt_.params.maxDeg;
            if (ang > 1) ang = 1;
            if (ang < -1) ang = -1;
            frame_.rect(cx + (int)lrintf(ang * half) - 1, by - 3, 3, 16, col::YELLOW);
            int sw = (int)lrintf(tilt_.steer() * half);
            frame_.rect(sw >= 0 ? cx : cx + sw, by + 14, sw >= 0 ? sw + 1 : -sw, 4, col::GREEN);
            uint8_t spr = SPR_CAR_RED;
            if (tilt_.steer() < -0.3f) spr = SPR_CAR_RED_LEFT;
            if (tilt_.steer() > 0.3f) spr = SPR_CAR_RED_RIGHT;
            frame_.sprite(spr, cx, by + 60);
            break;
        }
        case 3: {  // CALIBRACION
            const float* n = tilt_.neutralVec();
            line(calibrated_ ? "NEUTRO (G):" : "SIN CALIBRAR", calibrated_ ? col::WHITE : col::ORANGE);
            if (calibrated_) {
                snprintf(buf, sizeof(buf), " X %+.3f", n[0]);
                line(buf, col::WHITE);
                snprintf(buf, sizeof(buf), " Y %+.3f", n[1]);
                line(buf, col::WHITE);
                snprintf(buf, sizeof(buf), " Z %+.3f", n[2]);
                line(buf, col::WHITE);
                snprintf(buf, sizeof(buf), "ANGULO NEUTRO %+.1f\x7f", tilt_.neutralDeg());
                line(buf, col::WHITE);
            }
            y += 6;
            if (axisKnown_) {
                snprintf(buf, sizeof(buf), "EJE DE GIRO: %c%c", "XYZ"[tilt_.params.axis],
                         tilt_.params.sign > 0 ? '+' : '-');
                line(buf, col::GREEN);
                line("(GUARDADO EN NVS)", col::GREY);
            } else {
                line("EJE DE GIRO: SIN CONFIGURAR", col::ORANGE);
            }
            y += 6;
            line("LARGO: CALIBRA EL NEUTRO", col::GREY);
            line("Y PIDE INCLINAR A LA", col::GREY);
            line("DERECHA PARA HALLAR EL EJE", col::GREY);
            break;
        }
        case 4: {  // RENDIMIENTO
            snprintf(buf, sizeof(buf), "FPS %.1f (OBJETIVO %d)", renderer_.fps, RENDER_FPS);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "CUADRO %lu US", (unsigned long)renderer_.lastFrameUs);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "DIBUJO CPU %lu US", (unsigned long)renderer_.lastRenderUs);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "SIM %.1f HZ (OBJ %d)", simHz_, SIM_HZ);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "TICK %lu US", (unsigned long)tickUsAvg_);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "SENSOR %.0f HZ", imuHz_);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "SPI LCD %lu MHZ", (unsigned long)(p_->lcdSpiHz() / 1000000));
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "HEAP LIBRE %lu KB", (unsigned long)(p_->freeHeap() / 1024));
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "HEAP MINIMO %lu KB", (unsigned long)(p_->minFreeHeap() / 1024));
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "ELEMENTOS %d%s", frame_.count, frame_.overflow ? " LLENO!" : "");
            line(buf, frame_.overflow ? col::RED : col::WHITE);
            break;
        }
        case 5: {  // RED
            NetCounters nc = p_->netCounters();
            snprintf(buf, sizeof(buf), "MAC %02X:%02X:%02X:%02X:%02X:%02X", link_.myMac[0], link_.myMac[1],
                     link_.myMac[2], link_.myMac[3], link_.myMac[4], link_.myMac[5]);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "CANAL %d  ESP-NOW %s", p_->netChannel(), netOk_ ? "OK" : "ERROR");
            line(buf, netOk_ ? col::WHITE : col::RED);
            snprintf(buf, sizeof(buf), "ESTADO %s", linkName(link_.state));
            line(buf, link_.up() ? col::GREEN : col::YELLOW);
            if (link_.active()) {
                snprintf(buf, sizeof(buf), "ROL %s", link_.role == Role::Host ? "ANFITRION" : "CLIENTE");
                line(buf, col::WHITE);
                snprintf(buf, sizeof(buf), "PAR ..%02X:%02X:%02X SES %04X", link_.peerMac[3], link_.peerMac[4],
                         link_.peerMac[5], link_.session);
                line(buf, col::WHITE);
                snprintf(buf, sizeof(buf), "RTT %lu MS  RSSI %d", (unsigned long)link_.rttMs, link_.lastRssi);
                line(buf, col::WHITE);
                snprintf(buf, sizeof(buf), "RELOJ %+ld MS %s", (long)link_.clockOffset,
                         link_.clockSynced ? "SINC" : "--");
                line(buf, col::WHITE);
                snprintf(buf, sizeof(buf), "ULTIMO RX HACE %lu MS", (unsigned long)(now - link_.lastRxMs));
                line(buf, col::WHITE);
            }
            y += 4;
            snprintf(buf, sizeof(buf), "TX OK %lu FALLO %lu", (unsigned long)nc.txOk, (unsigned long)nc.txFail);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "RX %lu COLA LLENA %lu", (unsigned long)nc.rx, (unsigned long)nc.rxDropped);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "VIEJOS %lu AJENOS %lu", (unsigned long)link_.oldPackets,
                     (unsigned long)link_.foreignPackets);
            line(buf, col::WHITE);
            snprintf(buf, sizeof(buf), "MALOS %lu DEMORADOS %lu", (unsigned long)link_.badPackets,
                     (unsigned long)link_.agedPackets);
            line(buf, col::WHITE);
            break;
        }
    }
}
