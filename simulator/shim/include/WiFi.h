#pragma once
// Simulator shim: a fake Wi-Fi radio. Scans "find" a small fixed set of
// networks, joining always succeeds instantly, and AP mode reports the classic
// 192.168.4.1. This lets the REAL WifiSelectionActivity and File Transfer
// activity run unmodified; the web server itself is reached through the
// browser bridge (see WebServer.h shim), not a real socket.
#include <cstdint>
#include <cstring>
#include <string>

#include "Arduino.h"

typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
#define WIFI_OFF WIFI_MODE_NULL
#define WIFI_STA WIFI_MODE_STA
#define WIFI_AP WIFI_MODE_AP
#define WIFI_AP_STA WIFI_MODE_APSTA

typedef enum {
  WL_NO_SHIELD = 255,
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL,
  WL_SCAN_COMPLETED,
  WL_CONNECTED,
  WL_CONNECT_FAILED,
  WL_CONNECTION_LOST,
  WL_DISCONNECTED,
} wl_status_t;

typedef enum {
  WIFI_AUTH_OPEN = 0,
  WIFI_AUTH_WEP,
  WIFI_AUTH_WPA_PSK,
  WIFI_AUTH_WPA2_PSK,
  WIFI_AUTH_WPA_WPA2_PSK,
  WIFI_AUTH_WPA2_ENTERPRISE,
  WIFI_AUTH_WPA3_PSK,
  WIFI_AUTH_MAX
} wifi_auth_mode_t;

#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

// Sort/scan method enums (values irrelevant in the sim).
typedef enum { WIFI_FAST_SCAN = 0, WIFI_ALL_CHANNEL_SCAN } wifi_scan_method_t;
typedef enum { WIFI_CONNECT_AP_BY_SIGNAL = 0, WIFI_CONNECT_AP_BY_SECURITY } wifi_sort_method_t;

// Client type (NetworkClient in arduino-esp32 v3). There is no real socket:
// the WebServer shim points `sink` at the current response body, so handlers
// that stream raw bytes through client().write() (file downloads, WebDAV GET)
// still produce complete responses.
class NetworkClient {
 public:
  std::string* sink = nullptr;

  operator bool() const { return sink != nullptr; }
  bool connected() const { return sink != nullptr; }
  void stop() {}
  int read() { return -1; }
  int read(uint8_t*, size_t) { return -1; }
  void clear() {}
  size_t write(uint8_t b) {
    if (sink) sink->push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* buf, size_t len) {
    if (sink) sink->append(reinterpret_cast<const char*>(buf), len);
    return len;
  }
  // Stream-to-client (e.g. WebDAV GET streams a HalFile): drain via read().
  template <typename T>
  size_t write(T& stream) {
    if (!sink) return 0;
    uint8_t buf[1024];
    size_t total = 0;
    int n;
    while ((n = stream.read(buf, sizeof(buf))) > 0) {
      sink->append(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
      total += static_cast<size_t>(n);
    }
    return total;
  }
  IPAddress remoteIP() const { return IPAddress(127, 0, 0, 1); }
  uint16_t remotePort() const { return 0; }
  void setTimeout(uint32_t) {}
};
using WiFiClient = NetworkClient;

class WiFiClass {
 public:
  struct FakeNetwork {
    const char* ssid;
    int rssi;
    wifi_auth_mode_t auth;
    uint8_t channel;
  };
  static constexpr int FAKE_NETWORK_COUNT = 3;
  static const FakeNetwork FAKE_NETWORKS[FAKE_NETWORK_COUNT];

  // --- mode / connection -----------------------------------------------------
  bool mode(wifi_mode_t m) {
    mode_ = m;
    if (m == WIFI_MODE_NULL) status_ = WL_DISCONNECTED;
    return true;
  }
  wifi_mode_t getMode() { return mode_; }
  wl_status_t status() { return status_; }

  bool begin(const char* ssid, const char* = nullptr) {
    ssid_ = ssid ? ssid : "";
    status_ = WL_CONNECTED;
    if (mode_ == WIFI_MODE_NULL) mode_ = WIFI_MODE_STA;
    return true;
  }
  bool disconnect(bool = false, bool = false) {
    status_ = WL_DISCONNECTED;
    ssid_.clear();
    return true;
  }
  String SSID() { return String(ssid_); }
  int32_t RSSI() { return status_ == WL_CONNECTED ? -52 : 0; }
  IPAddress localIP() { return status_ == WL_CONNECTED ? IPAddress(192, 168, 1, 42) : IPAddress(); }
  uint8_t channel() { return 6; }

  // --- soft AP ---------------------------------------------------------------
  bool softAP(const char* ssid, const char* = nullptr, int = 1, int = 0, int = 4) {
    apSsid_ = ssid ? ssid : "";
    mode_ = WIFI_MODE_AP;
    return true;
  }
  IPAddress softAPIP() { return IPAddress(192, 168, 4, 1); }
  bool softAPdisconnect(bool = false) {
    apSsid_.clear();
    return true;
  }
  int softAPgetStationNum() { return 1; }  // pretend the user's browser has joined

  // --- scanning --------------------------------------------------------------
  int16_t scanNetworks(bool async = false, bool = false, bool = false, uint32_t = 300, uint8_t = 0,
                       const char* = nullptr, const uint8_t* = nullptr) {
    scanStartedAt_ = millis();
    scanning_ = true;
    if (async) return WIFI_SCAN_RUNNING;
    scanning_ = false;
    return FAKE_NETWORK_COUNT;
  }
  int16_t scanComplete() {
    if (!scanning_) return FAKE_NETWORK_COUNT;
    if (millis() - scanStartedAt_ < 700) return WIFI_SCAN_RUNNING;  // a little realism
    scanning_ = false;
    return FAKE_NETWORK_COUNT;
  }
  void scanDelete() { scanning_ = false; }
  String SSID(uint8_t i) { return String(valid(i) ? FAKE_NETWORKS[i].ssid : ""); }
  int32_t RSSI(uint8_t i) { return valid(i) ? FAKE_NETWORKS[i].rssi : -127; }
  wifi_auth_mode_t encryptionType(uint8_t i) { return valid(i) ? FAKE_NETWORKS[i].auth : WIFI_AUTH_OPEN; }
  uint8_t channel(uint8_t i) { return valid(i) ? FAKE_NETWORKS[i].channel : 1; }
  uint8_t* BSSID(uint8_t i) {
    static uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00};
    mac[5] = i;
    return mac;
  }
  uint8_t* BSSID(uint8_t* out) {  // current connection variant (esp32 v3)
    static uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0xAA};
    if (out) memcpy(out, mac, 6);
    return out ? out : mac;
  }
  String BSSIDstr(uint8_t i) {
    char buf[18];
    snprintf(buf, sizeof(buf), "DE:AD:BE:EF:00:%02X", i);
    return String(buf);
  }

  // --- misc ------------------------------------------------------------------
  String macAddress() { return String("AA:BB:CC:DD:EE:FF"); }
  uint8_t* macAddress(uint8_t* mac) {
    static const uint8_t kMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    memcpy(mac, kMac, 6);
    return mac;
  }
  bool setHostname(const char* name) {
    hostname_ = name ? name : "";
    return true;
  }
  const char* getHostname() { return hostname_.c_str(); }
  bool persistent(bool) { return true; }
  bool setAutoReconnect(bool) { return true; }
  bool setSleep(bool) { return true; }
  void setSortMethod(wifi_sort_method_t) {}
  void setScanMethod(wifi_scan_method_t) {}

 private:
  bool valid(uint8_t i) const { return i < FAKE_NETWORK_COUNT; }

  wifi_mode_t mode_ = WIFI_MODE_NULL;
  wl_status_t status_ = WL_DISCONNECTED;
  bool scanning_ = false;
  unsigned long scanStartedAt_ = 0;
  std::string ssid_;
  std::string apSsid_;
  std::string hostname_ = "crosspoint";
};
extern WiFiClass WiFi;
