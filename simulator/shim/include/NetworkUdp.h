#pragma once
// Simulator shim: the UDP discovery listener never receives packets.
#include <cstdint>

#include "Arduino.h"

class NetworkUDP {
 public:
  uint8_t begin(uint16_t) { return 1; }
  void stop() {}
  int parsePacket() { return 0; }
  int read(uint8_t*, size_t) { return 0; }
  int read(char*, size_t) { return 0; }
  IPAddress remoteIP() { return IPAddress(); }
  uint16_t remotePort() { return 0; }
  int beginPacket(IPAddress, uint16_t) { return 1; }
  size_t write(const uint8_t*, size_t len) { return len; }
  int endPacket() { return 1; }
};
