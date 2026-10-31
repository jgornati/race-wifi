#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint8_t kSettingsVersion = 1;
constexpr int kDiagPages = 6;
constexpr uint8_t kSensitivity[] = {15, 20, 25, 30, 35};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

// ------------------------------------------------------------------ arranque y loop

void App::begin(Platform* p) {
    p_ = p;
    track_.build();
    race_.init(&track_);
    race_.resetGrid(1);
    renderer_.begin(p);

    if (!p_->loadSettings(settings_) || settings_.version != kSettingsVersion) {
        settings_.version = kSettingsVersion;
        settings_.steerAxis = -1;
        settings_.steerSign = 1;
        settings_.maxDeg = (uint8_t)TILT_MAX_DEG;
    }
    axisKnown_ = settings_.steerAxis >= 0 && settings_.steerAxis <= 2;
    tilt_.params.axis = axisKnown_ ? settings_.steerAxis : 0;
    tilt_.params.sign = settings_.steerSign < 0 ? -1 : 1;
    tilt_.params.deadzoneDeg = TILT_DEADZONE_DEG;
    tilt_.params.maxDeg = (settings_.maxDeg >= 8 && settings_.maxDeg <= 60) ? settings_.maxDeg : TILT_MAX_DEG;
    tilt_.params.filterMs = TILT_FILTER_MS;

    netOk_ = p_->netBegin(ESPNOW_CHANNEL);
    link_.begin(p_);

    uint32_t now = p_->millis();
    lastTickUs_ = p_->micros64();
    nextFrameMs_ = now;
    nextNetMs_ = now;
    simWindowMs_ = now;
    lastActivityMs_ = now;
    setScreen(Screen::Boot, now);

    char buf[128];
    snprintf(buf, sizeof(buf), "[app] v%s imu=%d net=%d eje=%d signo=%d max=%.0f", FW_VERSION, p_->imuPresent(),
             netOk_, settings_.steerAxis, settings_.steerSign, tilt_.params.maxDeg);
    p_->log(buf);
}

void App::loop() {
    uint32_t now = p_->millis();

    BtnEvent be = button_.update(p_->buttonDown(), now);

    // La callback de ESP-NOW solo copia a una cola; aca se procesa.
    RxPacket rx;
    for (int budget = 16; budget > 0 && p_->netReceive(rx); budget--) {
        const PktHeader* h = link_.handle(rx, now);
        if (h) onGamePacket(*h, rx, now);
    }
    link_.update(now);
    handleLinkEvents(now);
    if (be != BtnEvent::None) onButton(be, now);

    // simulacion a paso fijo
    const int64_t tickUs = 1000000 / SIM_HZ;
    int64_t nowUs = p_->micros64();
    // La largada se detecta con la precision del loop y el paso de simulacion se alinea
    // a ese instante: asi las dos placas mueven los autos a la vez, no un tick despues.
    if (checkStart(now)) lastTickUs_ = nowUs;
    int steps = 0;
    while (nowUs - lastTickUs_ >= tickUs && steps < 4) {
        lastTickUs_ += tickUs;
        tick(1.0f / SIM_HZ, now);
        steps++;
    }
    if (nowUs - lastTickUs_ > tickUs * 4) lastTickUs_ = nowUs - tickUs;  // no acumular atraso

    // red saliente: periodica y tambien inmediata ante cambios
    if (stateDirty_ || inputDirty_ || (int32_t)(now - nextNetMs_) >= 0) {
        nextNetMs_ = now + 1000 / NET_HZ;
        stateDirty_ = inputDirty_ = false;
        sendNet(now);
    }

    // dibujo: se arma un cuadro nuevo solo cuando el anterior termino de salir
    renderer_.service();
    if (!renderer_.busy() && (int32_t)(now - nextFrameMs_) >= 0) {
        nextFrameMs_ += 1000 / RENDER_FPS;
        if ((int32_t)(now - nextFrameMs_) > 0) nextFrameMs_ = now + 1000 / RENDER_FPS;
        compose(now);
        renderer_.start(frame_);
        renderer_.service();
    }
}

void App::tick(float dt, uint32_t now) {
    int64_t t0 = p_->micros64();
    ImuSample s;
    imuValid_ = p_->imuLatest(s);
    if (imuValid_) {
        imu_ = s;
        if (calibrated_) tilt_.update(s, dt);
    }

    switch (screen_) {
        case Screen::Boot:
            if (now - screenSince_ > 1200) goSearch(now);
            break;
        case Screen::Calibrate:
            if (imuValid_ && calib_.add(imu_, now) == Calibrator::Done) {
                tilt_.setNeutral(calib_.mean());
                calibrated_ = true;
                char buf[96];
                snprintf(buf, sizeof(buf), "[cal] neutro g=(%.3f %.3f %.3f) angulo=%.1f", calib_.mean()[0],
                         calib_.mean()[1], calib_.mean()[2], tilt_.neutralDeg());
                p_->log(buf);
                if (!axisKnown_ || afterCalib_ == Screen::Diag) {
                    wizard_.start(calib_.mean());
                    setScreen(Screen::AxisSetup, now);
                } else {
                    goAfterCalibration(now);
                }
            }
            break;
        case Screen::AxisSetup:
            if (imuValid_ && wizard_.add(imu_, now) == AxisWizard::Done) {
                settings_.steerAxis = (int8_t)wizard_.axis();
                settings_.steerSign = (int8_t)wizard_.sign();
                axisKnown_ = true;
                tilt_.params.axis = settings_.steerAxis;
                tilt_.params.sign = settings_.steerSign;
                tilt_.reconfigure();
                saveSettings();
                char buf[64];
                snprintf(buf, sizeof(buf), "[cal] eje de giro=%c signo=%+d", "XYZ"[wizard_.axis()], wizard_.sign());
                p_->log(buf);
                goAfterCalibration(now);
            }
            break;
        default:
            break;
    }

    // Sin interruptor de encendido: si nadie la usa, se duerme sola. Al dormir espera a
    // que se suelte BOOT (si no, el mismo boton la despertaria enseguida).
    Phase ph = phase();
    bool racing = screen_ == Screen::Game && (solo_ || link_.up()) && (ph == Phase::Countdown || ph == Phase::Race);
    if (racing || screen_ == Screen::Calibrate || screen_ == Screen::AxisSetup || screen_ == Screen::Diag)
        lastActivityMs_ = now;
    if (screen_ != Screen::Sleep && now - lastActivityMs_ >= SLEEP_IDLE_MS) goSleep(now);
    if (screen_ == Screen::Sleep && now - screenSince_ >= SLEEP_SCREEN_MS && !button_.down()) p_->powerOff();

    if (link_.active() && link_.role == Role::Host) {
        hostTick(dt, now);
    } else if (link_.active() && link_.role == Role::Client) {
        clientTick(dt, now);
    } else if (solo_ && screen_ == Screen::Game) {
        soloTick(dt, now);
    }
    updateCamera(dt);
    fx_.update(dt * 1000.0f);

    uint32_t us = (uint32_t)(p_->micros64() - t0);
    tickUsAvg_ = (tickUsAvg_ * 15 + us) / 16;
    ticksWindow_++;
    if (now - simWindowMs_ >= 1000) {
        simHz_ = (float)ticksWindow_ * 1000.0f / (float)(now - simWindowMs_);
        ticksWindow_ = 0;
        simWindowMs_ = now;
    }
}

// ------------------------------------------------------------------ transiciones

void App::setScreen(Screen s, uint32_t now) {
    screen_ = s;
    screenSince_ = now;
}

void App::goSearch(uint32_t now) {
    solo_ = false;
    localReady_ = localRematch_ = false;
    clientReady_ = clientRematch_ = false;
    cv_ = ClientView();
    race_.resetGrid(1);
    fx_.clear();
    if (netOk_) link_.startSearch(now);
    setScreen(Screen::Search, now);
    p_->log("[app] buscando rival");
}

void App::goSolo(uint32_t now) {
    link_.stop(now);
    solo_ = true;
    p_->log("[app] practica solo");
    if (!calibrated_)
        goCalibrate(now, Screen::Game);
    else
        goGame(now);
}

void App::goCalibrate(uint32_t now, Screen after) {
    afterCalib_ = after;
    localReady_ = false;
    calib_.reset(now);
    setScreen(Screen::Calibrate, now);
}

void App::goAfterCalibration(uint32_t now) {
    if (afterCalib_ == Screen::Diag)
        setScreen(Screen::Diag, now);
    else
        goGame(now);
}

void App::goGame(uint32_t now) {
    if (solo_) {
        uint8_t id = race_.raceId + 1;
        race_.resetGrid(1);
        race_.raceId = id;
        fx_.clear();
    }
    camInit_ = false;
    lastLap_ = 1;
    setScreen(Screen::Game, now);
}

void App::goDiag(uint32_t now) {
    link_.stop(now);
    solo_ = false;
    diagPage_ = 0;
    setScreen(Screen::Diag, now);
    p_->log("[app] modo diagnostico");
}

void App::goSleep(uint32_t now) {
    link_.stop(now);  // si estaba conectada, la otra placa recibe el aviso de salida
    solo_ = false;
    setScreen(Screen::Sleep, now);
    p_->log("[app] apagando");
}

void App::onConnected(uint32_t now) {
    cv_ = ClientView();
    clientReady_ = clientRematch_ = false;
    localReady_ = localRematch_ = false;
    clientSteer_ = 0;
    fx_.clear();
    if (link_.role == Role::Host) {
        uint8_t id = race_.raceId + 1;
        race_.resetGrid(2);
        race_.raceId = id;
        stateDirty_ = true;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "[net] conectado como %s sesion=%04X par=%02X:%02X:%02X",
             link_.role == Role::Host ? "ANFITRION" : "CLIENTE", link_.session, link_.peerMac[3], link_.peerMac[4],
             link_.peerMac[5]);
    p_->log(buf);
    if (screen_ == Screen::Search || screen_ == Screen::Game) {
        if (calibrated_)
            goGame(now);
        else
            goCalibrate(now, Screen::Game);
    }
}

void App::handleLinkEvents(uint32_t now) {
    LinkEvents e = link_.takeEvents();
    if (e.connected) onConnected(now);
    if (e.lost) {
        p_->log(e.peerLeft ? "[net] el rival salio" : "[net] conexion perdida");
        localReady_ = false;
        cv_.localRacing = false;  // el cliente congela su prediccion hasta saber como sigue
        if (link_.role == Role::Host) {
            clientReady_ = false;
            if (race_.phase == Phase::Race) {
                race_.paused = true;
            } else if (race_.phase == Phase::Countdown) {
                if (race_.ticks() == 0) {
                    race_.phase = Phase::Lobby;
                    race_.readyMask = 0;
                } else {
                    race_.phase = Phase::Race;
                    race_.paused = true;
                }
            }
            stateDirty_ = true;
        }
    }
    if (e.recovered) {
        p_->log("[net] conexion recuperada");
        if (link_.role == Role::Host && race_.phase == Phase::Race && race_.paused) {
            race_.startCountdown(now, RESUME_COUNTDOWN_MS);
            stateDirty_ = true;
        }
    }
    if (e.peerRestarted && screen_ != Screen::Diag && screen_ != Screen::Sleep) {
        p_->log("[net] el rival volvio a buscar: re-emparejando");
        goSearch(now);
    }
}

void App::onButton(BtnEvent ev, uint32_t now) {
    lastActivityMs_ = now;
    switch (screen_) {
        case Screen::Boot:
        case Screen::Sleep:
            break;
        case Screen::Search:
            if (ev == BtnEvent::Click) goSolo(now);
            else if (ev == BtnEvent::Long) goDiag(now);
            else if (ev == BtnEvent::VeryLong) goSleep(now);
            break;
        case Screen::Calibrate:
        case Screen::AxisSetup:
            if (ev == BtnEvent::VeryLong) {
                if (afterCalib_ == Screen::Diag && calibrated_)
                    setScreen(Screen::Diag, now);
                else
                    goSearch(now);
            }
            break;
        case Screen::Diag:
            if (ev == BtnEvent::Click) {
                diagPage_ = (diagPage_ + 1) % kDiagPages;
            } else if (ev == BtnEvent::Long) {
                diagAction(now);
            } else if (ev == BtnEvent::VeryLong) {
                if (link_.active())
                    calibrated_ ? goGame(now) : goCalibrate(now, Screen::Game);
                else
                    goSearch(now);
            }
            break;
        case Screen::Game: {
            if (!solo_ && link_.state == LinkState::Lost) {
                if (ev != BtnEvent::None) goSearch(now);  // volver a emparejar
                break;
            }
            if (ev == BtnEvent::VeryLong) {
                goSearch(now);
                break;
            }
            Phase ph = phase();
            if (ph == Phase::Lobby) {
                if (ev == BtnEvent::Click) {
                    if (solo_) {
                        race_.startCountdown(now, COUNTDOWN_MS);
                    } else {
                        localReady_ = !localReady_;
                        inputDirty_ = stateDirty_ = true;
                    }
                } else if (ev == BtnEvent::Long) {
                    goCalibrate(now, Screen::Game);
                }
            } else if (ph == Phase::Results) {
                if (ev == BtnEvent::Click) {
                    if (solo_) {
                        goGame(now);
                        race_.startCountdown(now, COUNTDOWN_MS);
                    } else {
                        localRematch_ = true;
                        inputDirty_ = stateDirty_ = true;
                    }
                } else if (ev == BtnEvent::Long) {
                    goCalibrate(now, Screen::Game);
                }
            }
            break;
        }
    }
}

void App::diagAction(uint32_t now) {
    switch (diagPage_) {
        case 2: {  // sensibilidad
            int n = sizeof(kSensitivity) / sizeof(kSensitivity[0]);
            int i = 0;
            while (i < n && kSensitivity[i] != (uint8_t)tilt_.params.maxDeg) i++;
            settings_.maxDeg = kSensitivity[(i + 1) % n];
            tilt_.params.maxDeg = settings_.maxDeg;
            saveSettings();
            break;
        }
        case 3:  // calibrar y detectar eje
            goCalibrate(now, Screen::Diag);
            break;
        case 5:  // prueba de enlace
            if (link_.state == LinkState::Off)
                link_.startSearch(now);
            else
                link_.stop(now);
            break;
        default:
            break;
    }
}

void App::saveSettings() {
    settings_.version = kSettingsVersion;
    p_->saveSettings(settings_);
}

// ------------------------------------------------------------------ simulacion

float App::localSteer() const { return (calibrated_ && screen_ == Screen::Game) ? tilt_.steer() : 0.0f; }

void App::raceEffects(const RaceEvents& ev) {
    for (int i = 0; i < 2; i++)
        if (ev.hit[i].happened) fx_.crash(ev.hit[i].s, ev.hit[i].x);
    if (ev.bump.happened) fx_.spark(ev.bump.s, ev.bump.x);
}

void App::hostTick(float dt, uint32_t now) {
    if (race_.phase == Phase::Lobby) {
        uint8_t m = (localReady_ && screen_ == Screen::Game ? 1 : 0) | (clientReady_ && link_.up() ? 2 : 0);
        if (m != race_.readyMask) {
            race_.readyMask = m;
            stateDirty_ = true;
        }
        if (m == 3) {
            race_.startCountdown(now, COUNTDOWN_MS);
            localReady_ = clientReady_ = false;
            stateDirty_ = true;
        }
    } else if (race_.phase == Phase::Results) {
        uint8_t m = (localRematch_ ? 1 : 0) | (clientRematch_ && link_.up() ? 2 : 0);
        if (m != race_.rematchMask) {
            race_.rematchMask = m;
            stateDirty_ = true;
        }
        if (m == 3) {
            uint8_t id = race_.raceId + 1;
            race_.resetGrid(2);
            race_.raceId = id;
            race_.startCountdown(now, COUNTDOWN_MS);
            localRematch_ = clientRematch_ = false;
            fx_.clear();
            lastLap_ = 1;
            stateDirty_ = true;
        }
    }

    float steer[2] = {localSteer(), link_.up() ? clientSteer_ : 0.0f};
    RaceEvents ev;
    race_.step(dt, steer, now, ev);
    if (ev.phaseChanged) stateDirty_ = true;
    raceEffects(ev);
    if (race_.phase == Phase::Race && !race_.paused)
        for (int i = 0; i < 2; i++) fx_.emitCar(i, race_.cars[i], dt * 1000.0f);
}

void App::soloTick(float dt, uint32_t now) {
    float steer[2] = {localSteer(), 0.0f};
    RaceEvents ev;
    race_.step(dt, steer, now, ev);
    raceEffects(ev);
    if (race_.phase == Phase::Race) fx_.emitCar(0, race_.cars[0], dt * 1000.0f);
}

bool App::checkStart(uint32_t now) {
    if (link_.active() && link_.role == Role::Client) {
        // la largada es por reloj compartido: no espera al paquete del anfitrion
        if (!cv_.haveState || cv_.localRacing || !link_.clockSynced) return false;
        if ((Phase)cv_.st.phase != Phase::Countdown || (cv_.st.flags & ST_PAUSED)) return false;
        if ((int32_t)(link_.hostNow(now) - cv_.st.startHostMs) < 0) return false;
        cv_.localRacing = true;
        cv_.racingStartHostMs = cv_.st.startHostMs;
        return true;
    }
    bool authority = solo_ || (link_.active() && link_.role == Role::Host);
    if (!authority || race_.phase != Phase::Countdown) return false;
    if ((int32_t)(now - race_.startHostMs) < 0) return false;
    race_.phase = Phase::Race;
    race_.paused = false;
    stateDirty_ = true;
    return true;
}

void App::clientTick(float dt, uint32_t now) {
    if (!cv_.haveState) return;
    Phase ph = (Phase)cv_.st.phase;
    bool paused = cv_.st.flags & ST_PAUSED;
    bool racing = link_.up() && !paused && (ph == Phase::Race || (ph == Phase::Countdown && cv_.localRacing));
    if (!racing) return;

    HitEvent hit;
    carStep(cv_.me, track_, localSteer(), dt, &hit);
    if (hit.happened) fx_.crash(hit.s, hit.x);
    fx_.emitCar(1, cv_.me, dt * 1000.0f);
    cv_.hist.push(now, cv_.me);

    CarDraw r;
    if (rivalCar(now, r)) fx_.emitCar(0, r.c, dt * 1000.0f);
}

void App::onGamePacket(const PktHeader& h, const RxPacket& rx, uint32_t now) {
    if (h.type == PKT_INPUT) {
        PktInput in;
        memcpy(&in, rx.data, sizeof(in));
        clientSteer_ = clampf((float)in.steer / 127.0f, -1.0f, 1.0f);
        lastInputMs_ = now;
        if (race_.phase == Phase::Lobby) clientReady_ = in.flags & IN_READY;
        if (race_.phase == Phase::Results) clientRematch_ = (in.flags & IN_REMATCH) && in.raceId == race_.raceId;
    } else if (h.type == PKT_STATE) {
        PktState st;
        memcpy(&st, rx.data, sizeof(st));
        applyState(st, now);
    }
}

void App::applyState(const PktState& st, uint32_t now) {
    bool fresh = !cv_.haveState || st.raceId != cv_.st.raceId;
    cv_.st = st;
    cv_.haveState = true;
    cv_.stateRxMs = now;

    Car host, mine;
    unpackCar(st.cars[0], host);
    unpackCar(st.cars[1], mine);
    uint32_t tLocal = link_.toLocal(st.h.timeMs);
    Phase ph = (Phase)st.phase;
    bool paused = st.flags & ST_PAUSED;

    if (fresh) {
        cv_.me = mine;
        cv_.hist.clear();
        cv_.rival.clear();
        cv_.localRacing = false;
        cv_.rivalHits = host.hits;
        cv_.rivalBumps = host.bumps;
        cv_.lastErrS = cv_.lastErrX = cv_.maxErrS = cv_.maxErrX = 0;
        cv_.corrections = cv_.snaps = 0;
        fx_.clear();
        lastLap_ = 1;
        camInit_ = false;
    } else if (host.hits != cv_.rivalHits) {
        fx_.crash(host.s + CAR_HALF_L, host.x);
        cv_.rivalHits = host.hits;
    }
    cv_.rivalBumps = host.bumps;
    cv_.rival.push(tLocal, host);

    // Una cuenta nueva (revancha o reanudacion tras un corte) anula la largada anterior:
    // mientras dure, el auto propio queda quieto como en el anfitrion.
    if (ph == Phase::Lobby || ph == Phase::Results || paused ||
        (ph == Phase::Countdown && st.startHostMs != cv_.racingStartHostMs))
        cv_.localRacing = false;
    bool predicting = !paused && (ph == Phase::Race || (ph == Phase::Countdown && cv_.localRacing));
    if (predicting) {
        reconcile(mine, tLocal);
        if (mine.bumps != cv_.me.bumps) {
            // choque entre autos: lo decide el anfitrion y el cliente lo aplica
            cv_.me.bumps = mine.bumps;
            if (cv_.me.v > mine.v) cv_.me.v = mine.v;
            if (cv_.me.penaltyMs < mine.penaltyMs) cv_.me.penaltyMs = mine.penaltyMs;
            fx_.spark((cv_.me.s + host.s) * 0.5f, (cv_.me.x + host.x) * 0.5f);
        }
    } else {
        cv_.me = mine;
        cv_.hist.clear();
    }
    cv_.me.finished = mine.finished;
    cv_.me.finishMs = mine.finishMs;
    if (ph != Phase::Lobby) localReady_ = false;
    if (ph != Phase::Results) localRematch_ = false;
}

void App::reconcile(const Car& host, uint32_t tLocal) {
    HistEntry p;
    if (!cv_.hist.sample(tLocal, p)) {
        if (cv_.hist.size() == 0) {
            cv_.me.s = host.s;
            cv_.me.x = host.x;
            cv_.me.v = host.v;
            cv_.me.vx = host.vx;
        }
        return;
    }
    float es = host.s - p.s, ex = host.x - p.x, ev = host.v - p.v;
    cv_.lastErrS = es;
    cv_.lastErrX = ex;
    if (fabsf(es) > cv_.maxErrS) cv_.maxErrS = fabsf(es);
    if (fabsf(ex) > cv_.maxErrX) cv_.maxErrX = fabsf(ex);

    float k = RECON_BLEND, kv = 0.5f;
    if (fabsf(es) > RECON_SNAP_S || fabsf(ex) > RECON_SNAP_X) {
        k = kv = 1.0f;
        cv_.snaps++;
    } else {
        cv_.corrections++;
    }
    float ds = es * k, dx = ex * k, dv = ev * kv;
    cv_.me.s += ds;
    cv_.me.x += dx;
    cv_.me.v += dv;
    if (cv_.me.v < CAR_VMIN) cv_.me.v = CAR_VMIN;
    cv_.hist.shift(ds, dx, dv);
}

void App::updateCamera(float dt) {
    const Car& me = myCar();
    float c0 = track_.center(me.s);
    float target = track_.center(me.s + CAM_LOOKAHEAD) + CAM_FOLLOW_X * (me.x - c0);
    if (!camInit_) {
        camX_ = target;
        camInit_ = true;
    }
    float dtMs = dt * 1000.0f;
    camX_ += (target - camX_) * (dtMs / (CAM_SMOOTH_MS + dtMs));
}

void App::sendNet(uint32_t now) {
    if (!link_.active()) return;
    if (link_.role == Role::Host) {
        PktState st;
        memset(&st, 0, sizeof(st));
        st.phase = (uint8_t)race_.phase;
        st.raceId = race_.raceId;
        st.readyMask = race_.readyMask;
        st.rematchMask = race_.rematchMask;
        st.winner = race_.winner;
        st.flags = race_.paused ? ST_PAUSED : 0;
        st.leader = race_.leader();
        st.numCars = 2;
        st.startHostMs = race_.startHostMs;
        st.raceMs = race_.raceMs();
        st.cars[0] = packCar(race_.cars[0]);
        st.cars[1] = packCar(race_.cars[1]);
        link_.send(&st, sizeof(st), PKT_STATE, now);
    } else {
        PktInput in;
        memset(&in, 0, sizeof(in));
        in.steer = (int8_t)lrintf(localSteer() * 127.0f);
        in.flags = (localReady_ && calibrated_ && screen_ == Screen::Game ? IN_READY : 0) |
                   (localRematch_ ? IN_REMATCH : 0) | (calibrated_ ? IN_CALIBRATED : 0);
        in.raceId = cv_.haveState ? cv_.st.raceId : 0;
        link_.send(&in, sizeof(in), PKT_INPUT, now);
    }
}

// ------------------------------------------------------------------ consultas

Phase App::phase() const {
    if (link_.active() && link_.role == Role::Client) return cv_.haveState ? (Phase)cv_.st.phase : Phase::Lobby;
    return race_.phase;
}

int App::myIndex() const { return (link_.active() && link_.role == Role::Client) ? 1 : 0; }

const Car& App::myCar() const {
    if (link_.active() && link_.role == Role::Client) return cv_.me;
    return race_.cars[0];
}

bool App::rivalCar(uint32_t now, CarDraw& out) const {
    out = CarDraw();
    if (solo_ || !link_.active() || link_.state == LinkState::Lost) return false;
    if (link_.role == Role::Host) {
        out.c = race_.cars[1];
    } else {
        if (!cv_.rival.sample(now - RIVAL_INTERP_MS, out.c)) return false;
        Phase ph = phase();
        bool moving = (ph == Phase::Race && !(cv_.st.flags & ST_PAUSED)) || cv_.localRacing;
        if (RIVAL_PROJECT && moving) out.c.s += out.c.v * (RIVAL_INTERP_MS / 1000.0f);
    }
    out.visible = true;
    out.ghost = link_.stale(now);
    return true;
}

uint8_t App::winner() const {
    if (link_.active() && link_.role == Role::Client) return cv_.haveState ? cv_.st.winner : WINNER_NONE;
    return race_.winner;
}

int App::myPosition() const {
    if (solo_ || !link_.active()) return 1;
    if (link_.role == Role::Host) return race_.position(0);
    return (cv_.haveState && cv_.st.leader == 1) ? 1 : 2;
}

int32_t App::raceTimeMs(uint32_t now) const {
    if (!(link_.active() && link_.role == Role::Client)) return race_.raceMs();
    if (!cv_.haveState) return 0;
    int32_t t = cv_.st.raceMs;
    bool running = (Phase)cv_.st.phase == Phase::Race && !(cv_.st.flags & ST_PAUSED) && link_.up();
    if (running) t += (int32_t)(now - cv_.stateRxMs);
    return t;
}
