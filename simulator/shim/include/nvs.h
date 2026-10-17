#pragma once
// Simulator shim: ESP-IDF NVS (non-volatile key-value storage), as a
// process-lifetime in-memory store. Device-unique values (e.g. DeviceSecret)
// therefore regenerate per simulator run — the persistent identity lives on
// a real device's flash, which the simulator does not have.
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "esp_err.h"

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE = 1 } nvs_open_mode_t;
#ifndef ESP_ERR_NVS_NOT_FOUND
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#endif
#ifndef ESP_ERR_NVS_INVALID_LENGTH
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#endif

namespace simnvs {
inline std::map<std::string, std::vector<uint8_t>>& store() {
  static std::map<std::string, std::vector<uint8_t>> s;
  return s;
}
inline std::vector<std::string>& namespaces() {
  static std::vector<std::string> n;
  return n;
}
inline std::string key(nvs_handle_t h, const char* k) {
  return (h < namespaces().size() ? namespaces()[h] : std::string()) + "/" + k;
}
}  // namespace simnvs

inline esp_err_t nvs_open(const char* ns, nvs_open_mode_t, nvs_handle_t* out) {
  auto& n = simnvs::namespaces();
  for (size_t i = 0; i < n.size(); i++) {
    if (n[i] == ns) {
      *out = static_cast<nvs_handle_t>(i);
      return ESP_OK;
    }
  }
  n.emplace_back(ns);
  *out = static_cast<nvs_handle_t>(n.size() - 1);
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
inline esp_err_t nvs_set_blob(nvs_handle_t h, const char* k, const void* v, size_t len) {
  const auto* p = static_cast<const uint8_t*>(v);
  simnvs::store()[simnvs::key(h, k)] = std::vector<uint8_t>(p, p + len);
  return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t h, const char* k, void* out, size_t* len) {
  auto it = simnvs::store().find(simnvs::key(h, k));
  if (it == simnvs::store().end()) return ESP_ERR_NVS_NOT_FOUND;
  if (!out) {
    *len = it->second.size();
    return ESP_OK;
  }
  if (*len < it->second.size()) return ESP_ERR_NVS_INVALID_LENGTH;
  memcpy(out, it->second.data(), it->second.size());
  *len = it->second.size();
  return ESP_OK;
}
#define SIMNVS_INT(T, SUF)                                                                                         \
  inline esp_err_t nvs_set_##SUF(nvs_handle_t h, const char* k, T v) { return nvs_set_blob(h, k, &v, sizeof(v)); } \
  inline esp_err_t nvs_get_##SUF(nvs_handle_t h, const char* k, T* v) {                                            \
    size_t len = sizeof(T);                                                                                        \
    return nvs_get_blob(h, k, v, &len);                                                                            \
  }
SIMNVS_INT(uint8_t, u8)
SIMNVS_INT(uint16_t, u16)
SIMNVS_INT(uint32_t, u32)
SIMNVS_INT(int32_t, i32)
#undef SIMNVS_INT
