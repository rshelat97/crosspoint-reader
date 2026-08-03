#pragma once
// Simulator shim: I2C bus does not exist on the host. Reads fail so callers
// take their "peripheral absent" paths.
#include <cstdint>

class TwoWire {
 public:
  bool begin(int = -1, int = -1, uint32_t = 0) { return true; }
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission(bool = true) { return 4; }  // 4 = other error
  size_t write(uint8_t) { return 1; }
  uint8_t requestFrom(uint8_t, uint8_t, bool = true) { return 0; }
  int available() { return 0; }
  int read() { return -1; }
};
extern TwoWire Wire;
