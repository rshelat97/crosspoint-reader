#pragma once
// Simulator shim: little-endian CRC32 identical to the ESP-IDF ROM routine
// (standard reflected CRC-32, init/final inversion handled by the caller's
// ~crc convention).
#include <cstddef>
#include <cstdint>

inline uint32_t esp_rom_crc32_le(uint32_t crc, const uint8_t* buf, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1u) - 1u)));
  }
  return ~crc;
}
