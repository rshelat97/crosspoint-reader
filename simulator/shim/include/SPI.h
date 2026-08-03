#pragma once
// Simulator shim: SPI bus does not exist on the host.
#include <cstdint>

class SPIClass {
 public:
  void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
  void end() {}
};
extern SPIClass SPI;
