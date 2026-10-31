// Pruebas en PC de la logica del firmware (sin hardware).
// Compila el mismo codigo de RaceWifi/src (todo salvo la capa ESP32) contra una
// plataforma simulada. Ver tools/host/run_host_tests.sh.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "../../RaceWifi/src/game/race.h"
#include "../../RaceWifi/src/game/track.h"
#include "../../RaceWifi/src/input/button.h"
#include "../../RaceWifi/src/input/tilt.h"
#include "../../RaceWifi/src/net/protocol.h"
#include "fake_platform.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (cond) {                                                         \
            g_pass++;                                                       \
        } else {                                                            \
            g_fail++;                                                       \
            printf("  FALLA %s:%d: %s -- ", __FILE__, __LINE__, #cond);     \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

static std::string g_out = "build/host_out";
static Track g_track;

// ------------------------------------------------------------------ "jugador" automatico

struct Bot {
    FakeBoard* b;
    float laneBias = 0;
    bool active = true;
    Screen lastScreen = Screen::Boot;
    uint32_t screenAt = 0;

    void step() {
        App& a = b->app;
        uint32_t now = b->millis();
        if (a.screen() != lastScreen) {
            lastScreen = a.screen();
            screenAt = now;
        }
        float tilt = 0;
        if (active && a.screen() == Screen::AxisSetup) {
            tilt = (now - screenAt) < 900 ? 30.0f : 0.0f;  // inclina a la derecha y vuelve
        } else if (active && a.screen() == Screen::Game &&
                   (a.phase() == Phase::Race || a.phase() == Phase::Countdown)) {
            const Car& me = a.myCar();
            float lane = laneBias;
            float ws = g_track.wrap(me.s);
            for (int i = 0; i < g_track.obstacleCount(); i++) {
                const Obstacle& o = g_track.obstacle(i);
                float ds = o.s - ws;
                if (ds < -Track::kLength / 2) ds += Track::kLength;
                if (ds > Track::kLength / 2) ds -= Track::kLength;
                if (ds < 0 || ds > 150) continue;
                if (fabsf(o.off - lane) < 20) lane = (o.off > 0) ? o.off - 28 : o.off + 28;
            }
            if (lane > 36) lane = 36;
            if (lane < -36) lane = -36;
            float target = g_track.center(me.s + 60) + lane;
            float steer = (target - me.x) / 22.0f;
            if (steer > 1) steer = 1;
            if (steer < -1) steer = -1;
            float mag = fabsf(steer);
            tilt = mag < 0.02f ? 0 : (TILT_DEADZONE_DEG + mag * (TILT_MAX_DEG - TILT_DEADZONE_DEG)) * (steer < 0 ? -1 : 1);
        }
        b->tiltDeg = tilt;
    }
};

struct World {
    Medium m;
    std::vector<FakeBoard*> boards;
    std::vector<Bot> bots;
    std::function<void()> probe;

    void stepHalfMs() {
        g_nowUs += 500;
        m.deliver();
        for (auto& bt : bots) bt.step();
        for (auto* b : boards)
            if (b->online) b->app.loop();
        if (probe) probe();
    }
    void run(uint32_t ms) {
        for (uint32_t i = 0; i < ms * 2; i++) stepHalfMs();
    }
    bool until(const std::function<bool()>& p, uint32_t maxMs) {
        for (uint32_t i = 0; i < maxMs * 2; i++) {
            if (p()) return true;
            stepHalfMs();
        }
        return p();
    }
    void shot(FakeBoard* b, const char* name) {
        uint32_t before = b->framesPushed;
        until([&] { return b->framesPushed > before + 1 && !b->app.renderer().busy(); }, 500);
        b->savePpm(g_out + "/" + name + ".ppm");
    }
};

static FakeBoard* makeBoard(World& w, const char* name, uint8_t last, int32_t skew) {
    uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xA0, 0x00, last};
    FakeBoard* b = new FakeBoard(name, mac, &w.m);
    b->clockSkewMs = skew;
    b->quietLogs = true;
    w.boards.push_back(b);
    return b;
}

static void dumpLogs(FakeBoard* b, size_t from = 0) {
    for (size_t i = from; i < b->logs.size(); i++) printf("    %s\n", b->logs[i].c_str());
}

// ------------------------------------------------------------------ pruebas unitarias

static void testTrack() {
    printf("[pista]\n");
    float maxStep = 0;
    for (float s = 0; s < Track::kLength * 2; s += 4) {
        float d = fabsf(g_track.center(s + 4) - g_track.center(s));
        if (d > maxStep) maxStep = d;
    }
    CHECK(maxStep < 2.5f, "pendiente maxima %.2f px cada 4 px", maxStep);
    CHECK(fabsf(g_track.center(0) - g_track.center(Track::kLength)) < 0.01f, "vuelta no cerrada");
    CHECK(fabsf(g_track.center(-40) - g_track.center(Track::kLength - 40)) < 0.01f, "s negativo");
    for (int i = 0; i < g_track.obstacleCount(); i++) {
        const Obstacle& o = g_track.obstacle(i);
        float freeL = (o.off - o.halfW) - (-(Track::kHalfWidth - Track::kCurbW));
        float freeR = (Track::kHalfWidth - Track::kCurbW) - (o.off + o.halfW);
        CHECK(freeL >= 2 * CAR_HALF_W + 2 || freeR >= 2 * CAR_HALF_W + 2, "obstaculo %d tapa la pista", i);
    }
    printf("  largo %.0f px, %d obstaculos, %d decorados, pendiente max %.2f\n", Track::kLength,
           g_track.obstacleCount(), g_track.decorCount(), maxStep);
}

static void testTilt() {
    printf("[inclinacion]\n");
    Tilt t;
    t.params.axis = 1;
    t.params.sign = 1;
    float neutral[3] = {1, 0, 0};
    t.setNeutral(neutral);
    auto feed = [&](float deg) {
        float r = deg * 3.14159265f / 180.0f;
        ImuSample s = {};
        s.ax = cosf(r);
        s.ay = sinf(r);
        s.az = 0;
        for (int i = 0; i < 120; i++) t.update(s, 1.0f / 60);
        return t.steer();
    };
    CHECK(feed(2.0f) == 0.0f, "2 grados deberia caer en la zona muerta");
    CHECK(fabsf(feed(14.0f) - 0.5f) < 0.02f, "14 grados -> 0.5, dio %.3f", t.steer());
    CHECK(feed(25.0f) > 0.99f, "25 grados -> 1");
    CHECK(feed(40.0f) == 1.0f, "satura en 1");
    CHECK(feed(-14.0f) < -0.48f, "negativo");
    // filtro: un escalon no llega de golpe
    feed(0);
    ImuSample s = {};
    s.ax = cosf(0.4f);
    s.ay = sinf(0.4f);
    t.update(s, 1.0f / 60);
    CHECK(t.angleDeg() < 0.3f * 22.9f, "el filtro deberia suavizar el escalon (%.1f)", t.angleDeg());
}

static void testCalibration() {
    printf("[calibracion]\n");
    Calibrator c;
    c.reset(0);
    ImuSample s = {};
    Calibrator::Status st = Calibrator::Collecting;
    uint32_t t = 0;
    for (int i = 0; i < 200 && st != Calibrator::Done; i++) {
        t = (uint32_t)i * 10;
        s.ax = 0.02f + ((i * 37) % 7 - 3) * 0.001f;
        s.ay = -0.01f;
        s.az = 1.0f;
        s.count = (uint32_t)i + 1;
        st = c.add(s, t);
    }
    CHECK(st == Calibrator::Done, "no termino quieta");
    CHECK(t >= CALIB_WINDOW_MS && t < CALIB_WINDOW_MS + 100, "termino en %u ms", t);
    c.reset(0);
    int moving = 0;
    for (int i = 0; i < 300; i++) {
        s.ax = sinf(i * 0.3f) * 0.3f;
        s.count = (uint32_t)i + 1000;
        if (c.add(s, (uint32_t)i * 10) == Calibrator::Moving) moving++;
        CHECK(c.add(s, (uint32_t)i * 10) != Calibrator::Done || false, "no deberia calibrar en movimiento");
    }
    CHECK(moving > 5, "deberia detectar movimiento");

    AxisWizard w;
    float base[3] = {1, 0, 0};
    w.start(base);
    ImuSample q = {};
    uint32_t now = 0;
    for (int i = 0; i < 100; i++, now += 10) {  // inclina a la derecha: crece +Y
        q.ax = cosf(0.5f);
        q.ay = sinf(0.5f);
        w.add(q, now);
    }
    for (int i = 0; i < 20; i++, now += 10) {
        q.ax = 1;
        q.ay = 0;
        w.add(q, now);
    }
    CHECK(w.status() == AxisWizard::Done && w.axis() == 1 && w.sign() == 1, "eje %d signo %d", w.axis(), w.sign());
}

static void testButton() {
    printf("[boton]\n");
    Button b;
    std::vector<BtnEvent> ev;
    auto run = [&](uint32_t from, uint32_t to, bool down) {
        for (uint32_t t = from; t < to; t++) {
            BtnEvent e = b.update(down, t);
            if (e != BtnEvent::None) ev.push_back(e);
        }
    };
    run(0, 100, false);
    run(100, 250, true);
    run(250, 400, false);
    CHECK(ev.size() == 1 && ev[0] == BtnEvent::Click, "click");
    ev.clear();
    run(400, 1600, true);
    run(1600, 1700, false);
    CHECK(ev.size() == 1 && ev[0] == BtnEvent::Long, "largo");
    ev.clear();
    run(1700, 4800, true);
    CHECK(ev.size() == 1 && ev[0] == BtnEvent::VeryLong, "muy largo mientras se mantiene");
    run(4800, 5000, false);
    CHECK(ev.size() == 1, "al soltar un muy largo no hay otro evento");
    ev.clear();
    for (uint32_t t = 5000; t < 5100; t++) {  // rebote de 10 ms no cuenta
        BtnEvent e = b.update((t / 3) % 2 == 0 && t < 5010, t);
        if (e != BtnEvent::None) ev.push_back(e);
    }
    CHECK(ev.empty(), "rebote corto genero %zu eventos", ev.size());
}

static void testProtocol() {
    printf("[protocolo]\n");
    CHECK(seqNewer(1, 0) && !seqNewer(0, 1) && seqNewer(2, 65535) && !seqNewer(65535, 2), "secuencia circular");
    Car c;
    c.s = 12345.6f;
    c.x = -31.25f;
    c.v = 187.5f;
    c.vx = -12.0f;
    c.penaltyMs = 430;
    c.offroad = true;
    c.hits = 3;
    c.bumps = 2;
    c.steer = 0.8f;
    Car d;
    unpackCar(packCar(c), d);
    CHECK(fabsf(d.s - c.s) < 0.01f && fabsf(d.x - c.x) < 0.2f && fabsf(d.v - c.v) < 0.2f, "ida y vuelta");
    CHECK(d.penaltyMs == 430 && d.offroad && d.hits == 3 && d.bumps == 2 && d.steer > 0, "flags");
    printf("  STATE %zu bytes, INPUT %zu bytes, HELLO %zu bytes\n", sizeof(PktState), sizeof(PktInput),
           sizeof(PktHello));
    CHECK(sizeof(PktState) <= 80, "STATE chico");
}

static void testNoStuck() {
    printf("[colisiones]\n");
    // un auto que va recto hacia cada obstaculo nunca queda trabado
    for (int i = 0; i < g_track.obstacleCount(); i++) {
        const Obstacle& o = g_track.obstacle(i);
        Car c;
        c.s = o.s - 120;
        c.x = g_track.center(o.s) + o.off;
        c.v = CAR_VMAX;
        HitEvent h;
        int hits = 0, firstHit = -1, recovered = -1;
        for (int k = 0; k < 60 * 6; k++) {
            // insiste contra el obstaculo hasta pasarlo; despues sigue la pista
            float steer = c.s < o.s + 20 ? (g_track.center(c.s) + o.off - c.x) / 10.0f
                                         : (g_track.center(c.s + 40) - c.x) / 30.0f;
            carStep(c, g_track, steer, 1.0f / 60, &h);
            if (h.happened && firstHit < 0) firstHit = k;
            if (h.happened) hits++;
            if (firstHit >= 0 && recovered < 0 && c.v >= 0.95f * CAR_VMAX) recovered = k;
        }
        CHECK(hits >= 1, "obstaculo %d sin choque", i);
        CHECK(c.s > o.s + 200, "obstaculo %d: quedo trabado en s=%.0f", i, c.s - o.s);
        float recMs = (recovered - firstHit) * 1000.0f / 60.0f;
        CHECK(recovered > 0 && recMs < 3000, "obstaculo %d: tardo %.0f ms en recuperar", i, recMs);
        if (i == 0) printf("  recuperacion tras chocar: %.0f ms hasta 95%% de la velocidad maxima\n", recMs);
    }
    // dos autos encimados se separan y siguen
    Car a, b;
    a.s = 1000;
    b.s = 1004;
    a.x = b.x = g_track.center(1000);
    a.v = b.v = 200;
    HitEvent ev;
    bool bumped = carsResolve(a, b, g_track, &ev);
    CHECK(bumped && fabsf(a.x - b.x) >= 2 * CAR_HALF_W, "separacion %.1f", fabsf(a.x - b.x));
    CHECK(a.v < b.v, "el de atras pierde mas velocidad");
}

// ------------------------------------------------------------------ escenarios completos

static void testTwoBoards() {
    printf("[dos placas: emparejar, carrera, corte, revancha, salida]\n");
    World w;
    w.m.loss = 0.03;
    FakeBoard* A = makeBoard(w, "ROJO", 0x92, 0);
    FakeBoard* B = makeBoard(w, "AZUL", 0x31, 987654);  // relojes distintos
    A->app.begin(A);
    B->app.begin(B);
    w.bots = {Bot{A, -10}, Bot{B, 10}};

    w.run(1250);
    w.shot(B, "01_buscando");
    bool paired = w.until([&] { return A->app.link().up() && B->app.link().up(); }, 5000);
    CHECK(paired, "no se emparejaron");
    CHECK(A->app.link().role == Role::Host && B->app.link().role == Role::Client, "roles por MAC");
    CHECK(A->app.link().session == B->app.link().session && A->app.link().session != 0, "sesion");
    printf("  emparejadas a los %.2f s, sesion %04X\n", g_nowUs / 1e6, A->app.link().session);

    w.until([&] { return A->app.screen() == Screen::AxisSetup; }, 3000);
    w.shot(A, "02_configurar_giro");
    bool inGame = w.until([&] { return A->app.screen() == Screen::Game && B->app.screen() == Screen::Game; }, 8000);
    CHECK(inGame, "no llegaron al juego (A=%d B=%d)", (int)A->app.screen(), (int)B->app.screen());
    CHECK(A->app.tilt().params.axis == 1 && A->app.tilt().params.sign == 1, "eje detectado %d/%d",
          A->app.tilt().params.axis, A->app.tilt().params.sign);
    CHECK(B->app.link().clockSynced, "reloj sincronizado");
    printf("  offset de reloj medido %d ms (real %d), RTT %u ms\n", B->app.link().clockOffset, -987654,
           B->app.link().rttMs);
    CHECK(abs(B->app.link().clockOffset - (-987654)) <= 3, "offset de reloj");

    A->pressNow(80);
    w.run(400);
    w.shot(B, "03_listos_cliente");
    B->pressNow(80);
    CHECK(w.until([&] { return A->app.phase() == Phase::Countdown && B->app.phase() == Phase::Countdown; }, 1000),
          "cuenta regresiva");
    w.run(1200);
    w.shot(B, "04_cuenta_cliente");

    int64_t tA = -1, tB = -1;
    w.until([&] {
        if (tA < 0 && A->app.race().phase == Phase::Race) tA = g_nowUs;
        if (tB < 0 && B->app.client().localRacing) tB = g_nowUs;
        return tA >= 0 && tB >= 0;
    }, 5000);
    printf("  largada: diferencia real entre placas %.1f ms\n", (tB - tA) / 1000.0);
    CHECK(tA >= 0 && tB >= 0 && llabs(tB - tA) <= 3000, "largada desincronizada");

    // error de prediccion del cliente medido contra el estado real del anfitrion
    float maxDs = 0, maxDx = 0;
    double sumDs = 0;
    long nDs = 0;
    float minSpeed = 1e9f;
    std::vector<float> errS, errX;
    w.probe = [&] {
        if (A->app.race().phase != Phase::Race || A->app.race().paused || !B->app.link().up()) return;
        if (A->app.race().ticks() == 0) return;  // instante de largada: todavia en la grilla
        const Car& truth = A->app.race().cars[1];
        const Car& pred = B->app.client().me;
        if (truth.finished) return;
        float ds = fabsf(truth.s - pred.s), dx = fabsf(truth.x - pred.x);
        if (ds > maxDs) maxDs = ds;
        if (dx > maxDx) maxDx = dx;
        sumDs += ds;
        nDs++;
        errS.push_back(ds);
        errX.push_back(dx);
        if (truth.v < minSpeed) minSpeed = truth.v;
    };
    w.run(6000);
    w.shot(A, "05_carrera_anfitrion");
    w.shot(B, "06_carrera_cliente");

    // corte de 3 s en plena carrera
    float sBefore = A->app.race().cars[0].s;
    w.m.blackout = true;
    bool lost = w.until([&] { return A->app.link().state == LinkState::Lost && B->app.link().state == LinkState::Lost; }, 4000);
    CHECK(lost, "no detectaron la perdida");
    CHECK(A->app.race().paused, "el anfitrion deberia pausar");
    CarDraw rv;
    CHECK(!B->app.rivalCar(B->millis(), rv), "no debe mostrar al rival con la conexion perdida");
    w.shot(B, "07_sin_conexion");
    w.run(800);
    CHECK(fabsf(A->app.race().cars[0].s - sBefore) < 1200, "la carrera deberia estar pausada");
    w.m.blackout = false;
    bool back = w.until([&] { return A->app.link().up() && B->app.link().up(); }, 3000);
    CHECK(back, "no se recupero");
    CHECK(w.until([&] { return A->app.phase() == Phase::Countdown && B->app.phase() == Phase::Countdown; }, 1000),
          "cuenta para reanudar");
    float frozenS = B->app.myCar().s;
    w.shot(B, "08_reanuda");
    CHECK(!B->app.client().localRacing && fabsf(B->app.myCar().s - frozenS) < 0.5f,
          "durante la cuenta de reanudacion el cliente no debe mover su auto (%.1f px)", B->app.myCar().s - frozenS);
    CHECK(w.until([&] { return A->app.race().phase == Phase::Race && !A->app.race().paused; }, 4000), "reanudo");

    // un choque con obstaculo: chispa y humo en pantalla
    uint8_t hitsBefore = A->app.race().cars[0].hits;
    if (w.until([&] { return A->app.race().cars[0].hits != hitsBefore; }, 40000)) {
        w.run(60);
        w.shot(A, "13_choque_anfitrion");
    }
    // el azul deja de manejar un rato: se abre distancia y aparece la flecha del rival
    w.bots[1].active = false;
    bool gap = w.until([&] { return fabsf(A->app.race().cars[0].s - A->app.race().cars[1].s) > 420; }, 20000);
    CHECK(gap, "no se abrio distancia");
    w.shot(A, "14_flecha_anfitrion");
    w.shot(B, "15_flecha_cliente");
    w.bots[1].active = true;

    // hasta el final
    w.run(3000);
    float sA = A->app.race().cars[0].s, sB = A->app.race().cars[1].s;
    w.until([&] { return fabsf(A->app.race().cars[0].s - A->app.race().cars[1].s) > 300; }, 1);
    printf("  distancia entre autos tras reanudar %.0f px\n", fabsf(sA - sB));
    bool done = w.until([&] { return A->app.phase() == Phase::Results && B->app.phase() == Phase::Results; }, 200000);
    CHECK(done, "no terminaron");
    w.probe = nullptr;
    w.shot(A, "09_resultados_anfitrion");
    w.shot(B, "10_resultados_cliente");
    const Race& r = A->app.race();
    printf("  ganador %d (cliente ve %d); tiempos rojo %u ms azul %u ms\n", r.winner, B->app.winner(),
           r.cars[0].finishMs, r.cars[1].finishMs);
    printf("  choques obstaculo rojo %d azul %d, entre autos %d\n", r.cars[0].hits, r.cars[1].hits, r.cars[0].bumps);
    auto pct = [](std::vector<float> v, double p) {
        if (v.empty()) return 0.0f;
        std::sort(v.begin(), v.end());
        return v[(size_t)(p * (v.size() - 1))];
    };
    float s95 = pct(errS, 0.95), s99 = pct(errS, 0.99), x95 = pct(errX, 0.95), x99 = pct(errX, 0.99);
    printf("  prediccion cliente vs anfitrion (mismo instante real, %ld muestras):\n", nDs);
    printf("    s: prom %.2f  p95 %.1f  p99 %.1f  max %.1f px\n", nDs ? sumDs / nDs : 0.0, s95, s99, maxDs);
    printf("    x: p95 %.1f  p99 %.1f  max %.1f px\n", x95, x99, maxDx);
    printf("  reconciliacion: %u correcciones suaves, %u saltos; velocidad minima %.0f px/s\n",
           B->app.client().corrections, B->app.client().snaps, minSpeed);
    printf("  red: %llu paquetes, %llu perdidos (%.1f%%); FPS simulado %.1f\n", (unsigned long long)w.m.sent,
           (unsigned long long)w.m.dropped, 100.0 * w.m.dropped / (w.m.sent ? w.m.sent : 1), A->app.renderer().fps);
    CHECK(B->app.winner() == r.winner && r.winner != WINNER_NONE, "ganador distinto");
    CHECK(r.cars[0].finished && r.cars[1].finished, "ambos llegaron");
    CHECK(B->app.client().st.cars[0].finishMs == r.cars[0].finishMs, "tiempos iguales en ambas");
    CHECK(s99 < 12 && x99 < 8, "prediccion demasiado lejos del anfitrion (p99 s %.1f x %.1f)", s99, x99);
    CHECK(maxDs < RECON_SNAP_S && maxDx < RECON_SNAP_X, "error maximo por encima del umbral de salto");
    CHECK(minSpeed >= CAR_VMIN - 0.01f, "un auto quedo detenido");
    CHECK(A->app.renderer().fps > 25, "FPS %.1f", A->app.renderer().fps);

    // revancha
    uint8_t oldRace = r.raceId;
    A->pressNow(80);
    w.run(300);
    w.shot(B, "11_revancha_pedida");
    B->pressNow(80);
    CHECK(w.until([&] { return A->app.phase() == Phase::Countdown && B->app.phase() == Phase::Countdown; }, 1500),
          "revancha");
    CHECK(A->app.race().raceId != oldRace && B->app.client().st.raceId == A->app.race().raceId, "carrera nueva");
    w.until([&] { return A->app.race().phase == Phase::Race; }, 4000);
    w.run(2000);

    // el cliente sale a proposito (muy largo); el anfitrion lo ve y re-empareja
    uint16_t oldSession = A->app.link().session;
    B->pressNow(2700);
    CHECK(w.until([&] { return A->app.link().state == LinkState::Lost && A->app.link().peerLeftFlag; }, 4000),
          "el anfitrion no vio la salida");
    w.shot(A, "12_rival_salio");
    w.run(1500);
    CHECK(A->app.link().state == LinkState::Lost, "tras un BYE no debe re-emparejar sin que el jugador lo pida");
    CHECK(B->app.screen() == Screen::Search, "el que salio queda buscando (puede elegir practica)");
    A->pressNow(80);
    bool repaired = w.until([&] { return A->app.link().up() && B->app.link().up(); }, 6000);
    CHECK(repaired && A->app.link().session != oldSession, "re-emparejar con sesion nueva");
    CHECK(A->app.link().role == Role::Host && B->app.link().role == Role::Client, "roles estables");
    w.until([&] { return A->app.screen() == Screen::Game && B->app.screen() == Screen::Game; }, 3000);
    CHECK(A->app.phase() == Phase::Lobby && B->app.phase() == Phase::Lobby, "vuelven a la sala");
    printf("  logs del anfitrion:\n");
    dumpLogs(A);
    printf("  logs del cliente:\n");
    dumpLogs(B);
}

static void testRebootMidRace() {
    printf("[una placa se reinicia en carrera]\n");
    World w;
    FakeBoard* A = makeBoard(w, "ROJO", 0x92, 0);
    FakeBoard* B = makeBoard(w, "AZUL", 0x31, 5000);
    A->app.begin(A);
    B->app.begin(B);
    w.bots = {Bot{A, -10}, Bot{B, 10}};
    w.until([&] { return A->app.screen() == Screen::Game && B->app.screen() == Screen::Game; }, 12000);
    A->pressNow(80);
    B->pressNow(80);
    w.until([&] { return A->app.race().phase == Phase::Race; }, 5000);
    w.run(3000);
    // B se "apaga" y vuelve a arrancar desde cero (sin NVS: hace de nuevo el eje)
    B->online = false;
    w.run(2500);
    CHECK(A->app.link().state == LinkState::Lost, "A detecta la perdida");
    w.boards.pop_back();
    w.m.boards.pop_back();
    w.bots.pop_back();
    FakeBoard* B2 = makeBoard(w, "AZUL2", 0x31, 9000);
    B2->app.begin(B2);
    w.bots.push_back(Bot{B2, 10});
    bool again = w.until([&] { return A->app.link().up() && B2->app.link().up(); }, 6000);
    CHECK(again, "deberian re-emparejar solos al ver el HELLO del rival");
    CHECK(A->app.link().role == Role::Host, "rol estable");
}

static void testThreeBoards() {
    printf("[tres placas buscando a la vez]\n");
    World w;
    makeBoard(w, "X", 0x50, 0);
    makeBoard(w, "Y", 0x40, 111);
    makeBoard(w, "Z", 0x30, 222);
    for (auto* b : w.boards) b->app.begin(b);
    w.run(8000);
    int hosts = 0, clients = 0;
    for (auto* b : w.boards) {
        const NetLink& l = b->app.link();
        if (!l.up()) continue;
        FakeBoard* peer = nullptr;
        for (auto* o : w.boards)
            if (memcmp(o->mac, l.peerMac, 6) == 0) peer = o;
        CHECK(peer && memcmp(peer->app.link().peerMac, b->mac, 6) == 0, "%s: par no reciproco", b->name.c_str());
        CHECK(peer && peer->app.link().role != l.role, "%s: mismo rol que su par", b->name.c_str());
        CHECK(peer && peer->app.link().session == l.session, "sesion");
        if (l.role == Role::Host) hosts++;
        else clients++;
    }
    printf("  conectadas: %d anfitrion(es), %d cliente(s)\n", hosts, clients);
    CHECK(hosts == 1 && clients == 1, "deberia formarse exactamente un par");
}

static void testSolo() {
    printf("[practica solo]\n");
    World w;
    FakeBoard* S = makeBoard(w, "SOLO", 0x77, 0);
    S->app.begin(S);
    w.bots = {Bot{S, 0}};
    w.run(1400);
    CHECK(S->app.screen() == Screen::Search, "buscando");
    w.shot(S, "00_buscando");
    S->pressNow(80);
    CHECK(w.until([&] { return S->app.screen() == Screen::Calibrate; }, 1000), "calibrar");
    w.shot(S, "20_calibrando");
    CHECK(w.until([&] { return S->app.screen() == Screen::Game; }, 6000), "juego solo");
    w.shot(S, "21_solo_sala");
    S->pressNow(80);
    w.until([&] { return S->app.race().phase == Phase::Race; }, 5000);
    w.run(2500);
    w.shot(S, "22_solo_carrera");
    bool done = w.until([&] { return S->app.phase() == Phase::Results; }, 200000);
    CHECK(done, "no termino");
    w.shot(S, "23_solo_llegada");
    printf("  tiempo %u ms, choques %d\n", S->app.race().cars[0].finishMs, S->app.race().cars[0].hits);
    CHECK(S->app.race().cars[0].finishMs > 60000 && S->app.race().cars[0].finishMs < 130000, "tiempo razonable");
    CHECK(S->app.link().state == LinkState::Off, "sin red en practica");
}

static void testDiag() {
    printf("[diagnostico]\n");
    World w;
    FakeBoard* D = makeBoard(w, "DIAG", 0x66, 0);
    D->app.begin(D);
    w.run(1400);
    D->pressNow(1200);
    CHECK(w.until([&] { return D->app.screen() == Screen::Diag; }, 2000), "entra a diagnostico");
    D->tiltDeg = 12;
    const char* names[] = {"30_diag_pantalla", "31_diag_sensor", "32_diag_direccion", "33_diag_calibracion",
                           "34_diag_rendimiento", "35_diag_red"};
    for (int p = 0; p < 6; p++) {
        w.run(1100);
        w.shot(D, names[p]);
        D->pressNow(80);
        w.run(200);
    }
    D->tiltDeg = 0;
    D->pressNow(2700);
    CHECK(w.until([&] { return D->app.screen() == Screen::Search; }, 4000), "sale con muy largo");
}

static void testSleep() {
    printf("[apagado]\n");
    {
        World w;
        FakeBoard* S = makeBoard(w, "SUENO", 0x55, 0);
        S->app.begin(S);
        w.run(1400);
        S->pressNow(3000);
        CHECK(w.until([&] { return S->app.screen() == Screen::Sleep; }, 3000), "BOOT muy largo buscando: apagar");
        w.shot(S, "36_apagando");
        CHECK(!S->poweredOff, "espera que se suelte BOOT y que se vea CHAU");
        CHECK(w.until([&] { return S->poweredOff; }, 3000), "se duerme");
        CHECK(S->app.link().state == LinkState::Off, "red apagada");
    }
    {
        // olvidada prendida: se duerme sola
        World w;
        FakeBoard* T = makeBoard(w, "OLVIDO", 0x56, 0);
        T->app.begin(T);
        int64_t t0 = g_nowUs;
        CHECK(w.until([&] { return T->poweredOff; }, SLEEP_IDLE_MS + 5000), "se apaga sola sin uso");
        double secs = (g_nowUs - t0) / 1e6;
        printf("  sin uso se durmio a los %.0f s\n", secs);
        CHECK(secs >= SLEEP_IDLE_MS / 1000.0 && secs < SLEEP_IDLE_MS / 1000.0 + 5, "momento del apagado %.1f s", secs);
    }
    {
        // en carrera no se duerme aunque nadie toque BOOT
        World w;
        FakeBoard* R = makeBoard(w, "CORRE", 0x57, 0);
        R->app.begin(R);
        w.bots = {Bot{R, 0}};
        w.run(1400);
        R->pressNow(80);
        w.until([&] { return R->app.screen() == Screen::Game; }, 6000);
        R->pressNow(80);  // ultimo BOOT: de aca en mas solo corre
        int64_t tPress = g_nowUs;
        w.until([&] { return R->app.race().phase == Phase::Race; }, 5000);
        bool slept = w.until([&] { return R->poweredOff; }, SLEEP_IDLE_MS + 200000);
        double after = (g_nowUs - tPress) / 1e6;
        printf("  carrera de %.0f s sin tocar BOOT; se durmio %.0f s despues del ultimo BOOT\n",
               R->app.race().cars[0].finishMs / 1000.0, after);
        CHECK(slept && R->app.race().cars[0].finished, "termina la carrera y despues se duerme");
        CHECK(after >= SLEEP_IDLE_MS / 1000.0 + R->app.race().cars[0].finishMs / 1000.0,
              "en carrera no cuenta como inactividad (%.0f s)", after);
    }
}

int main(int argc, char** argv) {
    if (argc > 1) g_out = argv[1];
    g_track.build();
    testTrack();
    testTilt();
    testCalibration();
    testButton();
    testProtocol();
    testNoStuck();
    testTwoBoards();
    testRebootMidRace();
    testThreeBoards();
    testSolo();
    testDiag();
    testSleep();
    printf("\n%d verificaciones OK, %d fallas\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
