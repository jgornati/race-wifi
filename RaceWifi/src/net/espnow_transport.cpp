#include "espnow_transport.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include <atomic>

namespace {

const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
QueueHandle_t s_queue = nullptr;
std::atomic<uint32_t> s_txOk{0}, s_txFail{0}, s_rx{0}, s_drop{0};
uint8_t s_peer[6];
bool s_hasPeer = false;
uint8_t s_channel = 0;

// Contexto de la tarea WiFi: solo copiar y encolar, sin esperar.
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!s_queue || len <= 0 || len > RX_MAX_LEN) {
        s_drop++;
        return;
    }
    RxPacket p;
    memcpy(p.mac, info->src_addr, 6);
    p.len = (uint8_t)len;
    p.rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
    p.rxMs = millis();
    memcpy(p.data, data, (size_t)len);
    if (xQueueSend(s_queue, &p, 0) == pdTRUE)
        s_rx++;
    else
        s_drop++;
}

void onSent(const esp_now_send_info_t*, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS)
        s_txOk++;
    else
        s_txFail++;
}

bool addPeer(const uint8_t mac[6]) {
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;  // el canal actual de la interfaz
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

}  // namespace

namespace espnow {

bool begin(uint8_t ch) {
    s_queue = xQueueCreate(16, sizeof(RxPacket));
    if (!s_queue) return false;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_ps(WIFI_PS_NONE);  // sin ahorro de energia: menor latencia
    if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
    uint8_t primary = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    s_channel = primary;
    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSent);
    return addPeer(kBroadcast);
}

void myMac(uint8_t mac[6]) { esp_wifi_get_mac(WIFI_IF_STA, mac); }

bool setPeer(const uint8_t mac[6]) {
    if (s_hasPeer && memcmp(s_peer, mac, 6) == 0) return true;
    clearPeer();
    if (!addPeer(mac)) return false;
    memcpy(s_peer, mac, 6);
    s_hasPeer = true;
    return true;
}

void clearPeer() {
    if (s_hasPeer) esp_now_del_peer(s_peer);
    s_hasPeer = false;
}

bool send(const uint8_t* mac, const void* data, size_t len) {
    if (esp_now_send(mac ? mac : kBroadcast, (const uint8_t*)data, len) == ESP_OK) return true;
    s_txFail++;
    return false;
}

bool receive(RxPacket& out) { return s_queue && xQueueReceive(s_queue, &out, 0) == pdTRUE; }

NetCounters counters() { return NetCounters{s_txOk.load(), s_txFail.load(), s_rx.load(), s_drop.load()}; }

uint8_t channel() { return s_channel; }

}  // namespace espnow
