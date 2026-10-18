#pragma once
// Simulator shim: ESP-IDF Wi-Fi driver power-save control (no radio; no-op).
#include "esp_err.h"

typedef enum { WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;

inline esp_err_t esp_wifi_get_ps(wifi_ps_type_t* type) {
  if (type) *type = WIFI_PS_NONE;
  return ESP_OK;
}
inline esp_err_t esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }
inline esp_err_t esp_wifi_set_max_tx_power(int8_t) { return ESP_OK; }
inline esp_err_t esp_wifi_stop() { return ESP_OK; }
