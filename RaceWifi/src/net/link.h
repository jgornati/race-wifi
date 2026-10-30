// Enlace entre las dos placas sobre ESP-NOW: busqueda, emparejamiento, sesion,
// secuencias, sincronizacion de reloj y deteccion de perdida. No sabe nada del juego:
// le entrega al App los paquetes INPUT/STATE ya validados.
//
// Roles: cuando dos placas que buscan se escuchan, la de MAC mayor es anfitriona. Las
// dos calculan lo mismo, asi que nunca hay dos anfitriones. El anfitrion propone una
// sesion aleatoria (PAIR_REQ) solo a una MAC menor y el cliente solo la acepta de una
// MAC mayor.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../config.h"
#include "../platform/platform.h"
#include "protocol.h"

enum class LinkState : uint8_t { Off, Searching, Pairing, Connected, Lost };
enum class Role : uint8_t { None, Host, Client };

struct LinkEvents {
    bool connected = false;      // sesion nueva establecida
    bool lost = false;           // dejo de recibir paquetes
    bool recovered = false;      // volvieron los paquetes de la misma sesion
    bool peerLeft = false;       // el rival mando BYE
    bool peerRestarted = false;  // el rival volvio a buscar (reinicio)
};

class NetLink {
public:
    void begin(Platform* p);
    void startSearch(uint32_t now);
    void stop(uint32_t now);
    void update(uint32_t now);
    const PktHeader* handle(const RxPacket& rx, uint32_t now);
    bool send(void* pkt, size_t len, uint8_t type, uint32_t now);  // unicast al rival
    LinkEvents takeEvents();

    bool active() const { return state == LinkState::Connected || state == LinkState::Lost; }
    bool up() const { return state == LinkState::Connected; }
    bool stale(uint32_t now) const { return active() && now - lastRxMs > NET_STALE_MS; }
    uint32_t hostNow(uint32_t now) const { return now + (uint32_t)clockOffset; }
    uint32_t toLocal(uint32_t hostMs) const { return hostMs - (uint32_t)clockOffset; }

    LinkState state = LinkState::Off;
    Role role = Role::None;
    bool peerLeftFlag = false;
    uint8_t myMac[6] = {0};
    uint8_t peerMac[6] = {0};
    uint16_t session = 0;
    int32_t clockOffset = 0;     // reloj del anfitrion - reloj local
    uint32_t rttMs = 0;
    bool clockSynced = false;
    int8_t lastRssi = 0;
    uint32_t lastRxMs = 0;
    uint32_t connectedAtMs = 0;
    uint32_t sessionsStarted = 0;
    uint32_t badPackets = 0;     // magia/version/largo invalidos
    uint32_t oldPackets = 0;     // secuencia repetida o vieja
    uint32_t foreignPackets = 0; // otra sesion u otra placa
    uint32_t agedPackets = 0;    // esperaron demasiado en la cola
    uint32_t gamePackets = 0;

private:
    void header(PktHeader& h, uint8_t type, uint16_t sess, uint32_t now);
    bool sendTo(const uint8_t* mac, void* pkt, size_t len, uint8_t type, uint16_t sess, uint32_t now);
    void becomeConnected(Role r, const uint8_t mac[6], uint16_t sess, uint32_t now);
    void handlePong(const PktPong& pong, uint32_t t3);

    Platform* p_ = nullptr;
    uint16_t seq_ = 0;
    uint16_t lastSeq_[PKT_TYPE_COUNT] = {0};
    bool seqValid_[PKT_TYPE_COUNT] = {false};
    uint32_t nextHelloMs_ = 0, nextPairMs_ = 0, pairDeadlineMs_ = 0, nextPingMs_ = 0;
    uint32_t nonce_ = 0;
    int pingsSent_ = 0;
    LinkEvents ev_;
    struct ClockSample {
        int32_t offset;
        uint32_t rtt;
        bool valid;
    } samples_[8] = {};
    int sampleIdx_ = 0;
};
