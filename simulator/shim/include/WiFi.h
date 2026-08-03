#pragma once
// Simulator shim: Wi-Fi hardware does not exist. Everything reports
// "disconnected / radio off", so the network-dependent settings screens take
// their offline error paths instead of crashing.
#include <cstdint>

#include "Arduino.h"

typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
#define WIFI_OFF WIFI_MODE_NULL
#define WIFI_STA WIFI_MODE_STA
#define WIFI_AP WIFI_MODE_AP

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

class WiFiClass {
 public:
  wifi_mode_t getMode() { return WIFI_MODE_NULL; }
  bool mode(wifi_mode_t) { return true; }
  wl_status_t status() { return WL_DISCONNECTED; }
  bool disconnect(bool = false, bool = false) { return true; }
  IPAddress localIP() { return IPAddress(); }
  String SSID() { return String(); }
  bool persistent(bool) { return true; }
  int16_t scanNetworks(bool = false) { return 0; }
  int16_t scanComplete() { return 0; }
  void scanDelete() {}
  bool begin(const char*, const char* = nullptr) { return false; }
};
extern WiFiClass WiFi;
