#pragma once
// Simulator shim for Arduino's Print base class. HalFile, OpdsParser and the
// BMP converters inherit from it, so it must be a real polymorphic base with
// the same virtual surface the firmware overrides.
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "WString.h"  // Arduino's Print.h provides String to its includers

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t b) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (n < size) {
      if (write(buffer[n]) != 1) break;
      n++;
    }
    return n;
  }
  size_t write(const char* buffer, size_t size) { return write(reinterpret_cast<const uint8_t*>(buffer), size); }
  virtual void flush() {}
  size_t print(const char* s) { return s ? write(reinterpret_cast<const uint8_t*>(s), strlen(s)) : 0; }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(int v) { return printNumber(static_cast<long>(v)); }
  size_t print(unsigned int v) { return printNumber(static_cast<unsigned long>(v)); }
  size_t print(long v) { return printNumber(v); }
  size_t print(unsigned long v) { return printNumber(v); }
  size_t println(const char* s) {
    size_t n = print(s);
    return n + write(static_cast<uint8_t>('\n'));
  }
  size_t println() { return write(static_cast<uint8_t>('\n')); }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len <= 0) return 0;
    if (static_cast<size_t>(len) >= sizeof(buf)) len = sizeof(buf) - 1;
    return write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len));
  }

 private:
  size_t printNumber(long v) {
    char buf[24];
    int len = snprintf(buf, sizeof(buf), "%ld", v);
    return len > 0 ? write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len)) : 0;
  }
  size_t printNumber(unsigned long v) {
    char buf[24];
    int len = snprintf(buf, sizeof(buf), "%lu", v);
    return len > 0 ? write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(len)) : 0;
  }
};

// On device the SdFat/Arduino header chain gives Print includers Stream too.
#include "Stream.h"
