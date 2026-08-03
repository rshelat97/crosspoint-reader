#pragma once
// Simulator shim for mbedtls base64 decode (used by credential obfuscation).
#include <cstddef>
#include <cstdint>

#define MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL (-0x002A)
#define MBEDTLS_ERR_BASE64_INVALID_CHARACTER (-0x002C)

inline int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen, const unsigned char* src, size_t slen) {
  auto val = [](unsigned char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  size_t n = 0;
  uint32_t acc = 0;
  int bits = 0;
  // First pass: compute output length and validate.
  size_t outLen = 0;
  for (size_t i = 0; i < slen; i++) {
    const unsigned char c = src[i];
    if (c == '=' || c == '\r' || c == '\n') continue;
    if (val(c) < 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    outLen++;
  }
  outLen = (outLen * 3) / 4;
  *olen = outLen;
  if (!dst) return 0;  // length query mode
  if (dlen < outLen) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  for (size_t i = 0; i < slen; i++) {
    const unsigned char c = src[i];
    if (c == '=' || c == '\r' || c == '\n') continue;
    acc = (acc << 6) | static_cast<uint32_t>(val(c));
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      dst[n++] = static_cast<unsigned char>((acc >> bits) & 0xFF);
    }
  }
  *olen = n;
  return 0;
}
