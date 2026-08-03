#pragma once
// Simulator shim for the Arduino core. Provides exactly the surface the
// CrossPoint firmware consumes on the host/WASM build (see the simulator
// research notes in simulator/README.md for the inventory).
#include <cassert>  // the Arduino core chain exposes assert to every sketch
#include <cmath>    // ...and math functions
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Print.h"
#include "WString.h"

// --- time / scheduling -------------------------------------------------------
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield();

// --- randomness --------------------------------------------------------------
long random(long howbig);

// --- attributes that mean nothing on the host --------------------------------
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#ifndef DRAM_ATTR
#define DRAM_ATTR
#endif
#ifndef PROGMEM
#define PROGMEM
#endif

// --- GPIO stubs (HAL sim implementations never touch real pins) --------------
#ifndef INPUT
#define INPUT 0x01
#define OUTPUT 0x03
#define HIGH 0x1
#define LOW 0x0
#endif
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return 0; }
inline int analogRead(uint8_t) { return 0; }

#ifndef strlcpy
inline size_t sim_strlcpy(char* dst, const char* src, size_t size) {
  const size_t srclen = strlen(src);
  if (size) {
    const size_t n = srclen >= size ? size - 1 : srclen;
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return srclen;
}
#define strlcpy sim_strlcpy
#endif

// --- ESP class ---------------------------------------------------------------
class EspClass {
 public:
  uint32_t getFreeHeap();
  uint32_t getHeapSize();
  uint32_t getMinFreeHeap();
  uint32_t getMaxAllocHeap();
  [[noreturn]] void restart();
};
extern EspClass ESP;

inline void setCpuFrequencyMhz(uint32_t) {}
inline uint32_t getCpuFrequencyMhz() { return 160; }

// --- IPAddress (network activity stubs only) ---------------------------------
class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : a_(a), b_(b), c_(c), d_(d) {}
  String toString() const {
    char buf[20];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a_, b_, c_, d_);
    return String(buf);
  }

 private:
  uint8_t a_ = 0, b_ = 0, c_ = 0, d_ = 0;
};
