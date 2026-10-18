#pragma once
// Simulator shim: ESP-IDF hardware RNG, backed by the host's std::random_device.
#include <cstddef>
#include <cstdint>
#include <random>

inline uint32_t esp_random() {
  static std::random_device rd;
  return rd();
}
inline void esp_fill_random(void* buf, size_t len) {
  auto* p = static_cast<uint8_t*>(buf);
  for (size_t i = 0; i < len; i++) p[i] = static_cast<uint8_t>(esp_random());
}
