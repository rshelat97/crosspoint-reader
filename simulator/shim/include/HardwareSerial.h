#pragma once
// Simulator shim: serial console mapped to stdout. Used by lib/Logging and
// src/main-equivalent code (log output + the CMD:SCREENSHOT channel, which the
// simulator does not implement).
#include "Stream.h"
#include "WString.h"

class HardwareSerial : public Stream {
 public:
  void begin(unsigned long) {}
  void end() {}
  void setTxTimeoutMs(uint32_t) {}
  operator bool() const { return true; }  // NOLINT: Arduino semantics (if (Serial))
  String readStringUntil(char) { return String(); }
  size_t write(uint8_t b) override {
    fputc(b, stdout);
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    fwrite(buffer, 1, size, stdout);
    return size;
  }
  void flush() override { fflush(stdout); }
};

// The Arduino core's global serial object. lib/Logging binds `logSerial` to
// this by reference before redefining `Serial` as a macro, so the name must
// exist as a real object exactly once (defined in simulator/shim/shim.cpp).
extern HardwareSerial Serial;
