#pragma once
// Simulator shim for Arduino-ESP32's MD5Builder with a real MD5
// implementation (RFC 1321 style), so KOReader document IDs hash identically
// to the device.
#include <cstdint>
#include <cstring>

#include "WString.h"

class MD5Builder {
 public:
  void begin() {
    a_ = 0x67452301;
    b_ = 0xefcdab89;
    c_ = 0x98badcfe;
    d_ = 0x10325476;
    lo_ = 0;
    hi_ = 0;
  }

  void add(const uint8_t* data, size_t len) {
    uint32_t savedLo = lo_;
    lo_ = (savedLo + static_cast<uint32_t>(len)) & 0x1fffffff;
    if (lo_ < savedLo) hi_++;
    hi_ += static_cast<uint32_t>(len >> 29);

    size_t used = savedLo & 0x3f;
    if (used) {
      size_t available = 64 - used;
      if (len < available) {
        memcpy(&buffer_[used], data, len);
        return;
      }
      memcpy(&buffer_[used], data, available);
      data += available;
      len -= available;
      body(buffer_, 64);
    }
    if (len >= 64) {
      const size_t full = len & ~static_cast<size_t>(0x3f);
      body(data, full);
      data += full;
      len -= full;
    }
    memcpy(buffer_, data, len);
  }
  void add(const char* s) { add(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
  void add(const String& s) { add(s.c_str()); }

  void calculate() {
    size_t used = lo_ & 0x3f;
    buffer_[used++] = 0x80;
    size_t available = 64 - used;
    if (available < 8) {
      memset(&buffer_[used], 0, available);
      body(buffer_, 64);
      used = 0;
      available = 64;
    }
    memset(&buffer_[used], 0, available - 8);
    const uint32_t loBits = lo_ << 3;
    buffer_[56] = static_cast<uint8_t>(loBits);
    buffer_[57] = static_cast<uint8_t>(loBits >> 8);
    buffer_[58] = static_cast<uint8_t>(loBits >> 16);
    buffer_[59] = static_cast<uint8_t>(loBits >> 24);
    buffer_[60] = static_cast<uint8_t>((hi_ << 3) | (lo_ >> 29));
    buffer_[61] = static_cast<uint8_t>(hi_ >> 5);
    buffer_[62] = static_cast<uint8_t>(hi_ >> 13);
    buffer_[63] = static_cast<uint8_t>(hi_ >> 21);
    body(buffer_, 64);
    store(digest_, a_);
    store(digest_ + 4, b_);
    store(digest_ + 8, c_);
    store(digest_ + 12, d_);
  }

  void getBytes(uint8_t* out) const { memcpy(out, digest_, 16); }

  String toString() const {
    char hex[33];
    static const char* k = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
      hex[i * 2] = k[digest_[i] >> 4];
      hex[i * 2 + 1] = k[digest_[i] & 0xF];
    }
    hex[32] = '\0';
    return String(hex);
  }

 private:
  static void store(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
  }
  static uint32_t load(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
  }

  void body(const uint8_t* data, size_t size) {
    uint32_t a = a_, b = b_, c = c_, d = d_;
    for (size_t off = 0; off < size; off += 64) {
      const uint8_t* p = data + off;
      uint32_t m[16];
      for (int i = 0; i < 16; i++) m[i] = load(p + i * 4);
      const uint32_t sa = a, sb = b, sc = c, sd = d;

#define MD5_F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))
#define MD5_STEP(f, w, x, y, z, mi, t, s)     \
  do {                                        \
    (w) += f((x), (y), (z)) + (mi) + (t);     \
    (w) = ((w) << (s)) | ((w) >> (32 - (s))); \
    (w) += (x);                               \
  } while (0);

      MD5_STEP(MD5_F, a, b, c, d, m[0], 0xd76aa478, 7)
      MD5_STEP(MD5_F, d, a, b, c, m[1], 0xe8c7b756, 12)
      MD5_STEP(MD5_F, c, d, a, b, m[2], 0x242070db, 17)
      MD5_STEP(MD5_F, b, c, d, a, m[3], 0xc1bdceee, 22)
      MD5_STEP(MD5_F, a, b, c, d, m[4], 0xf57c0faf, 7)
      MD5_STEP(MD5_F, d, a, b, c, m[5], 0x4787c62a, 12)
      MD5_STEP(MD5_F, c, d, a, b, m[6], 0xa8304613, 17)
      MD5_STEP(MD5_F, b, c, d, a, m[7], 0xfd469501, 22)
      MD5_STEP(MD5_F, a, b, c, d, m[8], 0x698098d8, 7)
      MD5_STEP(MD5_F, d, a, b, c, m[9], 0x8b44f7af, 12)
      MD5_STEP(MD5_F, c, d, a, b, m[10], 0xffff5bb1, 17)
      MD5_STEP(MD5_F, b, c, d, a, m[11], 0x895cd7be, 22)
      MD5_STEP(MD5_F, a, b, c, d, m[12], 0x6b901122, 7)
      MD5_STEP(MD5_F, d, a, b, c, m[13], 0xfd987193, 12)
      MD5_STEP(MD5_F, c, d, a, b, m[14], 0xa679438e, 17)
      MD5_STEP(MD5_F, b, c, d, a, m[15], 0x49b40821, 22)

      MD5_STEP(MD5_G, a, b, c, d, m[1], 0xf61e2562, 5)
      MD5_STEP(MD5_G, d, a, b, c, m[6], 0xc040b340, 9)
      MD5_STEP(MD5_G, c, d, a, b, m[11], 0x265e5a51, 14)
      MD5_STEP(MD5_G, b, c, d, a, m[0], 0xe9b6c7aa, 20)
      MD5_STEP(MD5_G, a, b, c, d, m[5], 0xd62f105d, 5)
      MD5_STEP(MD5_G, d, a, b, c, m[10], 0x02441453, 9)
      MD5_STEP(MD5_G, c, d, a, b, m[15], 0xd8a1e681, 14)
      MD5_STEP(MD5_G, b, c, d, a, m[4], 0xe7d3fbc8, 20)
      MD5_STEP(MD5_G, a, b, c, d, m[9], 0x21e1cde6, 5)
      MD5_STEP(MD5_G, d, a, b, c, m[14], 0xc33707d6, 9)
      MD5_STEP(MD5_G, c, d, a, b, m[3], 0xf4d50d87, 14)
      MD5_STEP(MD5_G, b, c, d, a, m[8], 0x455a14ed, 20)
      MD5_STEP(MD5_G, a, b, c, d, m[13], 0xa9e3e905, 5)
      MD5_STEP(MD5_G, d, a, b, c, m[2], 0xfcefa3f8, 9)
      MD5_STEP(MD5_G, c, d, a, b, m[7], 0x676f02d9, 14)
      MD5_STEP(MD5_G, b, c, d, a, m[12], 0x8d2a4c8a, 20)

      MD5_STEP(MD5_H, a, b, c, d, m[5], 0xfffa3942, 4)
      MD5_STEP(MD5_H, d, a, b, c, m[8], 0x8771f681, 11)
      MD5_STEP(MD5_H, c, d, a, b, m[11], 0x6d9d6122, 16)
      MD5_STEP(MD5_H, b, c, d, a, m[14], 0xfde5380c, 23)
      MD5_STEP(MD5_H, a, b, c, d, m[1], 0xa4beea44, 4)
      MD5_STEP(MD5_H, d, a, b, c, m[4], 0x4bdecfa9, 11)
      MD5_STEP(MD5_H, c, d, a, b, m[7], 0xf6bb4b60, 16)
      MD5_STEP(MD5_H, b, c, d, a, m[10], 0xbebfbc70, 23)
      MD5_STEP(MD5_H, a, b, c, d, m[13], 0x289b7ec6, 4)
      MD5_STEP(MD5_H, d, a, b, c, m[0], 0xeaa127fa, 11)
      MD5_STEP(MD5_H, c, d, a, b, m[3], 0xd4ef3085, 16)
      MD5_STEP(MD5_H, b, c, d, a, m[6], 0x04881d05, 23)
      MD5_STEP(MD5_H, a, b, c, d, m[9], 0xd9d4d039, 4)
      MD5_STEP(MD5_H, d, a, b, c, m[12], 0xe6db99e5, 11)
      MD5_STEP(MD5_H, c, d, a, b, m[15], 0x1fa27cf8, 16)
      MD5_STEP(MD5_H, b, c, d, a, m[2], 0xc4ac5665, 23)

      MD5_STEP(MD5_I, a, b, c, d, m[0], 0xf4292244, 6)
      MD5_STEP(MD5_I, d, a, b, c, m[7], 0x432aff97, 10)
      MD5_STEP(MD5_I, c, d, a, b, m[14], 0xab9423a7, 15)
      MD5_STEP(MD5_I, b, c, d, a, m[5], 0xfc93a039, 21)
      MD5_STEP(MD5_I, a, b, c, d, m[12], 0x655b59c3, 6)
      MD5_STEP(MD5_I, d, a, b, c, m[3], 0x8f0ccc92, 10)
      MD5_STEP(MD5_I, c, d, a, b, m[10], 0xffeff47d, 15)
      MD5_STEP(MD5_I, b, c, d, a, m[1], 0x85845dd1, 21)
      MD5_STEP(MD5_I, a, b, c, d, m[8], 0x6fa87e4f, 6)
      MD5_STEP(MD5_I, d, a, b, c, m[15], 0xfe2ce6e0, 10)
      MD5_STEP(MD5_I, c, d, a, b, m[6], 0xa3014314, 15)
      MD5_STEP(MD5_I, b, c, d, a, m[13], 0x4e0811a1, 21)
      MD5_STEP(MD5_I, a, b, c, d, m[4], 0xf7537e82, 6)
      MD5_STEP(MD5_I, d, a, b, c, m[11], 0xbd3af235, 10)
      MD5_STEP(MD5_I, c, d, a, b, m[2], 0x2ad7d2bb, 15)
      MD5_STEP(MD5_I, b, c, d, a, m[9], 0xeb86d391, 21)

#undef MD5_F
#undef MD5_G
#undef MD5_H
#undef MD5_I
#undef MD5_STEP

      a += sa;
      b += sb;
      c += sc;
      d += sd;
    }
    a_ = a;
    b_ = b;
    c_ = c;
    d_ = d;
  }

  uint32_t a_ = 0, b_ = 0, c_ = 0, d_ = 0;
  uint32_t lo_ = 0, hi_ = 0;
  uint8_t buffer_[64] = {};
  uint8_t digest_[16] = {};
};
