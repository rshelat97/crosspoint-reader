#pragma once
// Simulator shim for Arduino-ESP32's base64 encoder.
#include <cstdint>

#include "WString.h"

class base64 {
 public:
  static String encode(const uint8_t* data, size_t len) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
      const uint32_t b0 = data[i];
      const uint32_t b1 = i + 1 < len ? data[i + 1] : 0;
      const uint32_t b2 = i + 2 < len ? data[i + 2] : 0;
      const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
      out += tbl[(triple >> 18) & 0x3F];
      out += tbl[(triple >> 12) & 0x3F];
      out += i + 1 < len ? tbl[(triple >> 6) & 0x3F] : '=';
      out += i + 2 < len ? tbl[triple & 0x3F] : '=';
    }
    return String(out);
  }
  static String encode(const String& s) { return encode(reinterpret_cast<const uint8_t*>(s.c_str()), s.length()); }
};
