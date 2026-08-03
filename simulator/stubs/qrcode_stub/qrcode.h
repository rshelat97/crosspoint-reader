#pragma once
// Simulator fallback for ricmoo/QRCode when the real library cannot be
// fetched (offline build). Generation fails, so the QR display screen shows
// its error path. The CMake build prefers the real library when the network
// allows.
#include <cstdint>

typedef struct QRCode {
  uint8_t version;
  uint8_t size;
  uint8_t ecc;
  uint8_t mode;
  uint8_t mask;
  uint8_t* modules;
} QRCode;

#define ECC_LOW 0
#define ECC_MEDIUM 1
#define ECC_QUARTILE 2
#define ECC_HIGH 3

inline uint16_t qrcode_getBufferSize(uint8_t version) {
  const uint16_t size = 4 * version + 17;
  return (size * size + 7) / 8;
}
inline int8_t qrcode_initText(QRCode*, uint8_t*, uint8_t, uint8_t, const char*) { return -1; }
inline bool qrcode_getModule(const QRCode*, uint8_t, uint8_t) { return false; }
