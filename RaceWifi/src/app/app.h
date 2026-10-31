// Maquina de estados del juego. Portable: depende solo de Platform.
//
// Pantallas locales: Boot -> Search -> Calibrate -> (AxisSetup la primera vez) -> Game,
// Diag y Sleep (la placa no tiene interruptor: se apaga durmiendola). Dentro de Game, la fase la decide el anfitrion (o la placa sola en practica):
// Lobby (listos) -> Countdown (3-2-1 compartido) -> Race -> Results (revancha).
#pragma once
#include <stdint.h>

#include "../config.h"
#include "../display/canvas.h"
#include "../game/fx.h"
#include "../game/race.h"
#include "../game/track.h"
#include "../input/button.h"
#include "../input/tilt.h"
#include "../net/link.h"
#include "../net/protocol.h"
#include "../net/smoothing.h"
#include "../platform/platform.h"

enum class Screen : uint8_t { Boot, Search, Calibrate, AxisSetup, Game, Diag, Sleep };

// Lo que el cliente sabe de la carrera (el anfitrion es la fuente oficial).
struct ClientView {
    bool haveState = false;
    PktState st;
    uint32_t stateRxMs = 0;
    Car me;                 // auto propio, predicho localmente
    History hist;
    InterpBuffer rival;
    bool localRacing = false;
    uint32_t racingStartHostMs = 0;   // largada (o reanudacion) que activo localRacing
    uint8_t rivalHits = 0, rivalBumps = 0;
    // metricas de reconciliacion
    float lastErrS = 0, lastErrX = 0, maxErrS = 0, maxErrX = 0;
    uint32_t corrections = 0, snaps = 0;
};

// Lo que se dibuja de cada auto en un cuadro.
struct CarDraw {
    bool visible = false;
    bool ghost = false;
    Car c;
};

class App {
public:
    void begin(Platform* p);
    void loop();

    // consultas para diagnostico y pruebas en PC
    Screen screen() const { return screen_; }
    bool isSolo() const { return solo_; }
    bool calibrated() const { return calibrated_; }
    const NetLink& link() const { return link_; }
    const Race& race() const { return race_; }
    const ClientView& client() const { return cv_; }
    const Renderer& renderer() const { return renderer_; }
    const Frame& frame() const { return frame_; }
    const Tilt& tilt() const { return tilt_; }
    Phase phase() const;
    int myIndex() const;
    const Car& myCar() const;
    bool rivalCar(uint32_t now, CarDraw& out) const;
    uint8_t winner() const;
    int myPosition() const;
    int32_t raceTimeMs(uint32_t now) const;
    bool ready() const { return localReady_; }
    uint32_t tickUsAvg() const { return tickUsAvg_; }
    float simHz() const { return simHz_; }

private:
    // flujo
    void tick(float dt, uint32_t now);
    void onButton(BtnEvent ev, uint32_t now);
    void onGamePacket(const PktHeader& h, const RxPacket& rx, uint32_t now);
    void handleLinkEvents(uint32_t now);
    void sendNet(uint32_t now);
    void goSearch(uint32_t now);
    void goSolo(uint32_t now);
    void goCalibrate(uint32_t now, Screen after);
    void goAfterCalibration(uint32_t now);
    void goGame(uint32_t now);
    void goDiag(uint32_t now);
    void goSleep(uint32_t now);
    void setScreen(Screen s, uint32_t now);
    void onConnected(uint32_t now);

    // simulacion
    float localSteer() const;
    bool checkStart(uint32_t now);
    void hostTick(float dt, uint32_t now);
    void soloTick(float dt, uint32_t now);
    void clientTick(float dt, uint32_t now);
    void applyState(const PktState& st, uint32_t now);
    void reconcile(const Car& host, uint32_t tLocal);
    void raceEffects(const RaceEvents& ev);
    void updateCamera(float dt);
    void saveSettings();

    // dibujo (app_draw.cpp / app_diag.cpp)
    void compose(uint32_t now);
    void composeBoot(uint32_t now);
    void composeSearch(uint32_t now);
    void composeCalibrate(uint32_t now);
    void composeAxis(uint32_t now);
    void composeGame(uint32_t now);
    void composeDiag(uint32_t now);
    void composeSleep(uint32_t now);
    void composeLinkOverlay(uint32_t now);
    void drawWorldObjects(const CarDraw& me, const CarDraw& rival, uint32_t now);
    void drawHud(const CarDraw& me, const CarDraw& rival, uint32_t now);
    void drawCar(const Car& c, int colorIdx, bool ghost, uint32_t now);
    bool worldToScreen(float s, float x, int& sx, int& sy) const;
    void panel(int y, int h);
    void diagAction(uint32_t now);

    Platform* p_ = nullptr;
    Track track_;
    Race race_;
    Fx fx_;
    Button button_;
    Tilt tilt_;
    Calibrator calib_;
    AxisWizard wizard_;
    NetLink link_;
    Renderer renderer_;
    Frame frame_;
    ClientView cv_;
    Settings settings_ = {};

    Screen screen_ = Screen::Boot;
    Screen afterCalib_ = Screen::Game;
    uint32_t screenSince_ = 0;
    bool solo_ = false;
    bool calibrated_ = false;
    bool axisKnown_ = false;
    bool netOk_ = false;
    bool localReady_ = false;
    bool localRematch_ = false;
    bool stateDirty_ = false;
    bool inputDirty_ = false;

    // lado anfitrion: ultimo control recibido del cliente
    float clientSteer_ = 0;
    bool clientReady_ = false;
    bool clientRematch_ = false;
    uint32_t lastInputMs_ = 0;

    ImuSample imu_ = {};
    bool imuValid_ = false;
    int64_t lastTickUs_ = 0;
    uint32_t lastActivityMs_ = 0;  // ultimo BOOT o momento en carrera (apagado por inactividad)
    uint32_t nextNetMs_ = 0;
    uint32_t nextFrameMs_ = 0;
    float camX_ = 0;
    bool camInit_ = false;
    int diagPage_ = 0;
    uint32_t tickUsAvg_ = 0;
    uint32_t ticksWindow_ = 0;
    uint32_t simWindowMs_ = 0;
    float simHz_ = 0;
    int lastLap_ = 1;
    uint32_t lapBannerUntil_ = 0;
    uint32_t imuRateCount_ = 0;
    uint32_t imuRateMs_ = 0;
    float imuHz_ = 0;
};
