#include "link.h"

#include <string.h>

#include "../config.h"

namespace {
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
inline int macCmp(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6); }
}  // namespace

void NetLink::begin(Platform* p) {
    p_ = p;
    p_->netMyMac(myMac);
    nonce_ = p_->random32();
    seq_ = (uint16_t)p_->random32();
}

void NetLink::header(PktHeader& h, uint8_t type, uint16_t sess, uint32_t now) {
    h.magic = PROTO_MAGIC;
    h.version = PROTO_VERSION;
    h.type = type;
    h.flags = 0;
    h.session = sess;
    h.seq = ++seq_;
    h.timeMs = now;
}

bool NetLink::sendTo(const uint8_t* mac, void* pkt, size_t len, uint8_t type, uint16_t sess, uint32_t now) {
    header(*(PktHeader*)pkt, type, sess, now);
    return p_->netSend(mac, pkt, len);
}

bool NetLink::send(void* pkt, size_t len, uint8_t type, uint32_t now) {
    if (!active()) return false;
    return sendTo(peerMac, pkt, len, type, session, now);
}

LinkEvents NetLink::takeEvents() {
    LinkEvents e = ev_;
    ev_ = LinkEvents();
    return e;
}

void NetLink::startSearch(uint32_t now) {
    if (active()) {
        PktBye bye;
        sendTo(peerMac, &bye, sizeof(bye), PKT_BYE, session, now);
    }
    p_->netClearPeer();
    state = LinkState::Searching;
    role = Role::None;
    session = 0;
    peerLeftFlag = false;
    clockOffset = 0;
    clockSynced = false;
    nextHelloMs_ = now;
}

void NetLink::stop(uint32_t now) {
    if (active()) {
        PktBye bye;
        sendTo(peerMac, &bye, sizeof(bye), PKT_BYE, session, now);
    }
    p_->netClearPeer();
    state = LinkState::Off;
    role = Role::None;
    session = 0;
    peerLeftFlag = false;
}

void NetLink::becomeConnected(Role r, const uint8_t mac[6], uint16_t sess, uint32_t now) {
    memcpy(peerMac, mac, 6);
    p_->netSetPeer(peerMac);
    role = r;
    session = sess;
    state = LinkState::Connected;
    peerLeftFlag = false;
    lastRxMs = now;
    connectedAtMs = now;
    sessionsStarted++;
    for (int i = 0; i < PKT_TYPE_COUNT; i++) seqValid_[i] = false;
    for (auto& s : samples_) s.valid = false;
    clockOffset = 0;
    clockSynced = (r == Role::Host);  // el reloj oficial es el del anfitrion
    rttMs = 0;
    pingsSent_ = 0;
    nextPingMs_ = now;
    ev_.connected = true;
}

void NetLink::update(uint32_t now) {
    switch (state) {
        case LinkState::Off:
            break;
        case LinkState::Searching:
            if ((int32_t)(now - nextHelloMs_) >= 0) {
                PktHello hello;
                hello.nonce = nonce_;
                hello.searching = 1;
                hello.channel = p_->netChannel();
                sendTo(kBroadcast, &hello, sizeof(hello), PKT_HELLO, 0, now);
                nextHelloMs_ = now + NET_HELLO_MS + (p_->random32() % 60);
            }
            break;
        case LinkState::Pairing:
            if ((int32_t)(now - pairDeadlineMs_) >= 0) {
                p_->netClearPeer();
                state = LinkState::Searching;
                role = Role::None;
                nextHelloMs_ = now;
            } else if ((int32_t)(now - nextPairMs_) >= 0) {
                PktPairReq req;
                memcpy(req.target, peerMac, 6);
                sendTo(peerMac, &req, sizeof(req), PKT_PAIR_REQ, session, now);
                nextPairMs_ = now + NET_PAIR_RESEND_MS;
            }
            break;
        case LinkState::Connected:
        case LinkState::Lost:
            if (state == LinkState::Connected && now - lastRxMs > NET_LOST_MS) {
                state = LinkState::Lost;
                ev_.lost = true;
            }
            if (role == Role::Client && (int32_t)(now - nextPingMs_) >= 0) {
                PktPing ping;
                ping.t0 = now;
                sendTo(peerMac, &ping, sizeof(ping), PKT_PING, session, now);
                pingsSent_++;
                nextPingMs_ = now + (pingsSent_ < 15 ? NET_PING_FAST_MS : NET_PING_MS);
            }
            break;
    }
}

const PktHeader* NetLink::handle(const RxPacket& rx, uint32_t now) {
    if (rx.len < sizeof(PktHeader)) {
        badPackets++;
        return nullptr;
    }
    const PktHeader& h = *(const PktHeader*)rx.data;
    if (h.magic != PROTO_MAGIC || h.version != PROTO_VERSION || h.type == 0 || h.type >= PKT_TYPE_COUNT) {
        badPackets++;
        return nullptr;
    }
    if (macCmp(rx.mac, myMac) == 0) return nullptr;
    if (now - rx.rxMs > NET_MAX_PACKET_AGE_MS) {
        agedPackets++;
        return nullptr;
    }

    switch (h.type) {
        case PKT_HELLO: {
            if (rx.len < sizeof(PktHello)) {
                badPackets++;
                return nullptr;
            }
            const PktHello& hello = *(const PktHello*)rx.data;
            if (!hello.searching) return nullptr;
            if (state == LinkState::Searching && macCmp(myMac, rx.mac) > 0) {
                // MAC mayor: soy anfitrion y propongo una sesion nueva
                memcpy(peerMac, rx.mac, 6);
                p_->netSetPeer(peerMac);
                do {
                    session = (uint16_t)p_->random32();
                } while (session == 0);
                role = Role::Host;
                state = LinkState::Pairing;
                pairDeadlineMs_ = now + NET_PAIR_TIMEOUT_MS;
                nextPairMs_ = now;
            } else if (active() && !peerLeftFlag && macCmp(rx.mac, peerMac) == 0) {
                // El rival volvio a buscar sin despedirse (se reinicio): re-emparejar solos.
                // Si se fue a proposito (BYE) se espera a que este jugador apriete BOOT.
                ev_.peerRestarted = true;
            }
            return nullptr;
        }
        case PKT_PAIR_REQ: {
            if (rx.len < sizeof(PktPairReq)) {
                badPackets++;
                return nullptr;
            }
            const PktPairReq& req = *(const PktPairReq*)rx.data;
            if (macCmp(req.target, myMac) != 0 || macCmp(rx.mac, myMac) < 0 || h.session == 0) {
                foreignPackets++;
                return nullptr;
            }
            bool samePeer = active() && macCmp(rx.mac, peerMac) == 0;
            if (state == LinkState::Searching || (samePeer && h.session != session)) {
                becomeConnected(Role::Client, rx.mac, h.session, now);
            } else if (!(samePeer && h.session == session)) {
                return nullptr;  // ya estoy con otra placa
            }
            lastRxMs = now;
            PktPairAck ack;
            sendTo(peerMac, &ack, sizeof(ack), PKT_PAIR_ACK, session, now);
            return nullptr;
        }
        case PKT_PAIR_ACK:
            if (state == LinkState::Pairing && macCmp(rx.mac, peerMac) == 0 && h.session == session) {
                becomeConnected(Role::Host, rx.mac, session, now);
            }
            return nullptr;
        default:
            break;
    }

    // Paquetes de sesion: deben venir del rival y de la sesion actual
    if (!active() || macCmp(rx.mac, peerMac) != 0 || h.session != session) {
        foreignPackets++;
        return nullptr;
    }
    if (seqValid_[h.type] && !seqNewer(h.seq, lastSeq_[h.type])) {
        oldPackets++;
        return nullptr;
    }
    seqValid_[h.type] = true;
    lastSeq_[h.type] = h.seq;
    lastRxMs = now;
    lastRssi = rx.rssi;
    if (state == LinkState::Lost && !peerLeftFlag) {
        state = LinkState::Connected;
        ev_.recovered = true;
    }

    switch (h.type) {
        case PKT_PING:
            if (role == Role::Host && rx.len >= sizeof(PktPing)) {
                PktPong pong;
                pong.t0 = ((const PktPing*)rx.data)->t0;
                pong.tRx = rx.rxMs;
                pong.tTx = p_->millis();
                sendTo(peerMac, &pong, sizeof(pong), PKT_PONG, session, pong.tTx);
            }
            return nullptr;
        case PKT_PONG:
            if (role == Role::Client && rx.len >= sizeof(PktPong)) handlePong(*(const PktPong*)rx.data, rx.rxMs);
            return nullptr;
        case PKT_BYE:
            peerLeftFlag = true;
            if (state == LinkState::Connected) {
                state = LinkState::Lost;
                ev_.lost = true;
            }
            ev_.peerLeft = true;
            return nullptr;
        case PKT_INPUT:
            if (role != Role::Host || rx.len < sizeof(PktInput)) return nullptr;
            gamePackets++;
            return &h;
        case PKT_STATE:
            if (role != Role::Client || rx.len < sizeof(PktState)) return nullptr;
            gamePackets++;
            return &h;
        default:
            return nullptr;
    }
}

void NetLink::handlePong(const PktPong& pong, uint32_t t3) {
    uint32_t total = t3 - pong.t0;
    uint32_t hold = pong.tTx - pong.tRx;
    if (total > 2000 || hold > total) return;  // respuesta vieja o incoherente
    // NTP: offset = ((tRx - t0) + (tTx - t3)) / 2
    int32_t offset = ((int32_t)(pong.tRx - pong.t0) + (int32_t)(pong.tTx - t3)) / 2;
    samples_[sampleIdx_] = {offset, total - hold, true};
    sampleIdx_ = (sampleIdx_ + 1) % 8;
    // se usa la muestra con menor RTT de las ultimas 8 (la menos afectada por demoras)
    const ClockSample* best = nullptr;
    for (const auto& s : samples_)
        if (s.valid && (!best || s.rtt < best->rtt)) best = &s;
    clockOffset = best->offset;
    rttMs = total - hold;
    clockSynced = true;
}
