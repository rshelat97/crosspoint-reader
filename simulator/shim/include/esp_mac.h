#pragma once
// Simulator shim: fixed MAC for anything that wants a device identity.
#include <cstdint>

#include "esp_err.h"

typedef enum { ESP_MAC_WIFI_STA = 0, ESP_MAC_WIFI_SOFTAP, ESP_MAC_BT, ESP_MAC_ETH } esp_mac_type_t;

inline esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t) {
  const uint8_t fixed[6] = {0x53, 0x49, 0x4D, 0x00, 0x00, 0x01};  // "SIM"
  for (int i = 0; i < 6; i++) mac[i] = fixed[i];
  return ESP_OK;
}

inline esp_err_t esp_efuse_mac_get_default(uint8_t* mac) { return esp_read_mac(mac, ESP_MAC_WIFI_STA); }
