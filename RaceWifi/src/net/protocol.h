// Formato de los paquetes ESP-NOW. Ambas placas corren el mismo firmware, asi que se
// envian structs empaquetados (little-endian). Todo paquete lleva magia, version,
// sesion, numero de secuencia y el reloj del emisor.
#pragma once
#include <stdint.h>

#include "../game/race.h"

#define PROTO_MAGIC 0xA7
#define PROTO_VERSION 1

enum PktType : uint8_t {
    PKT_HELLO = 1,     // broadcast mientras busca rival
    PKT_PAIR_REQ = 2,  // anfitrion -> cliente: propone sesion
    PKT_PAIR_ACK = 3,  // cliente -> anfitrion: acepta
    PKT_PING = 4,      // cliente -> anfitrion: sincronizacion de reloj
    PKT_PONG = 5,
    PKT_INPUT = 6,     // cliente -> anfitrion: controles
    PKT_STATE = 7,     // anfitrion -> cliente: estado oficial
    PKT_BYE = 8,       // salida voluntaria
    PKT_TYPE_COUNT = 9
};

#pragma pack(push, 1)
struct PktHeader {
    uint8_t magic, version, type, flags;
    uint16_t session;
    uint16_t seq;
    uint32_t timeMs;   // reloj local del emisor al armar el paquete
};

struct PktHello {
    PktHeader h;
    uint32_t nonce;
    uint8_t searching;
    uint8_t channel;
};

struct PktPairReq {
    PktHeader h;
    uint8_t target[6];
};

struct PktPairAck {
    PktHeader h;
};

struct PktPing {
    PktHeader h;
    uint32_t t0;
};

struct PktPong {
    PktHeader h;
    uint32_t t0, tRx, tTx;
};

enum InputFlags : uint8_t { IN_READY = 1, IN_REMATCH = 2, IN_CALIBRATED = 4 };

struct PktInput {
    PktHeader h;
    int8_t steer;       // -127..127
    uint8_t flags;
    uint8_t raceId;
    uint8_t reserved;
};

enum CarFlags : uint8_t {
    CF_OFFROAD = 1,
    CF_FINISHED = 2,
    CF_LEFT = 4,
    CF_RIGHT = 8,
    CF_BRAKE = 16,
    CF_INVULN = 32,
};

struct CarNet {
    float s;
    int16_t x8, v8, vx8;   // en 1/8 de pixel
    uint8_t penalty10;     // ms / 10
    uint8_t invuln10;
    uint8_t flags;
    uint8_t hits, bumps;
    uint8_t lap;
    uint32_t finishMs;
};

enum StateFlags : uint8_t { ST_PAUSED = 1 };

struct PktState {
    PktHeader h;
    uint8_t phase, raceId, readyMask, rematchMask;
    uint8_t winner, flags, leader, numCars;
    uint32_t startHostMs;
    int32_t raceMs;
    CarNet cars[2];
};

struct PktBye {
    PktHeader h;
};
#pragma pack(pop)

static_assert(sizeof(PktHeader) == 12, "header");
static_assert(sizeof(PktState) <= 96, "STATE debe ser chico");

inline bool seqNewer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

CarNet packCar(const Car& c);
void unpackCar(const CarNet& n, Car& c);
