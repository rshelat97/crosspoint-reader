// Simulator implementations of the crypto backends the firmware links:
// wolfCrypt's C API (shim/include/wolfssl) and ContentProtection's
// WolfsslCrypto. Hashes are real so hashing call sites (plugin device IDs,
// LCP passphrase hashes) behave exactly as on device; ciphers, RSA and key
// exchange report failure — DRM-protected (LCP) books and the account
// plugin's device crypto are not simulated. Unprotected books never reach
// these paths.
#include <WolfsslCrypto.h>
#include <esp_random.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/random.h>

#include <cstring>

namespace {

uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  static constexpr uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const uint64_t bitLen = static_cast<uint64_t>(len) * 8;
  const size_t total = ((len + 9 + 63) / 64) * 64;  // message + 0x80 + 64-bit length, block-aligned
  for (size_t off = 0; off < total; off += 64) {
    uint8_t block[64];
    for (size_t i = 0; i < 64; i++) {
      const size_t p = off + i;
      if (p < len) {
        block[i] = data[p];
      } else if (p == len) {
        block[i] = 0x80;
      } else if (p >= total - 8) {
        block[i] = static_cast<uint8_t>(bitLen >> (8 * (total - 1 - p)));
      } else {
        block[i] = 0;
      }
    }
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      w[i] = (static_cast<uint32_t>(block[4 * i]) << 24) | (static_cast<uint32_t>(block[4 * i + 1]) << 16) |
             (static_cast<uint32_t>(block[4 * i + 2]) << 8) | block[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }
  for (int i = 0; i < 8; i++) {
    out[4 * i] = static_cast<uint8_t>(h[i] >> 24);
    out[4 * i + 1] = static_cast<uint8_t>(h[i] >> 16);
    out[4 * i + 2] = static_cast<uint8_t>(h[i] >> 8);
    out[4 * i + 3] = static_cast<uint8_t>(h[i]);
  }
}

void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  const uint64_t bitLen = static_cast<uint64_t>(len) * 8;
  const size_t total = ((len + 9 + 63) / 64) * 64;
  for (size_t off = 0; off < total; off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
      uint32_t v = 0;
      for (int j = 0; j < 4; j++) {
        const size_t p = off + 4 * i + j;
        uint8_t byteVal;
        if (p < len) {
          byteVal = data[p];
        } else if (p == len) {
          byteVal = 0x80;
        } else if (p >= total - 8) {
          byteVal = static_cast<uint8_t>(bitLen >> (8 * (total - 1 - p)));
        } else {
          byteVal = 0;
        }
        v = (v << 8) | byteVal;
      }
      w[i] = v;
    }
    for (int i = 16; i < 80; i++) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      const uint32_t t = rotl(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rotl(b, 30);
      b = a;
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  for (int i = 0; i < 5; i++) {
    out[4 * i] = static_cast<uint8_t>(h[i] >> 24);
    out[4 * i + 1] = static_cast<uint8_t>(h[i] >> 16);
    out[4 * i + 2] = static_cast<uint8_t>(h[i] >> 8);
    out[4 * i + 3] = static_cast<uint8_t>(h[i]);
  }
}

}  // namespace

// --- wolfCrypt C API ----------------------------------------------------------
int wc_Sha256Hash(const byte* data, word32 len, byte* hash) {
  sha256(data, len, hash);
  return 0;
}
int wc_ShaHash(const byte* data, word32 len, byte* hash) {
  sha1(data, len, hash);
  return 0;
}
int wc_InitRng(WC_RNG*) { return 0; }
int wc_RNG_GenerateBlock(WC_RNG*, byte* out, word32 sz) {
  esp_fill_random(out, sz);
  return 0;
}
int wc_FreeRng(WC_RNG*) { return 0; }
int wc_AesInit(Aes*, void*, int) { return 0; }
void wc_AesFree(Aes*) {}
int wc_AesSetKey(Aes*, const byte*, word32, const byte*, int) { return NOT_COMPILED_IN; }
int wc_AesCbcEncrypt(Aes*, byte*, const byte*, word32) { return NOT_COMPILED_IN; }
int wc_AesCbcDecrypt(Aes*, byte*, const byte*, word32) { return NOT_COMPILED_IN; }
int wc_AesGcmSetKey(Aes*, const byte*, word32) { return NOT_COMPILED_IN; }
int wc_AesGcmEncrypt(Aes*, byte*, const byte*, word32, const byte*, word32, byte*, word32, const byte*, word32) {
  return NOT_COMPILED_IN;
}
int wc_AesGcmDecrypt(Aes*, byte*, const byte*, word32, const byte*, word32, const byte*, word32, const byte*, word32) {
  return NOT_COMPILED_IN;
}

// --- ContentProtection backend -------------------------------------------------
namespace freeink {
namespace content {

WolfsslCrypto::WolfsslCrypto() { rngOk_ = true; }
WolfsslCrypto::~WolfsslCrypto() = default;

int32_t WolfsslCrypto::rsaPrivateRaw(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t) {
  lastError = "RSA not available in the simulator";
  return -1;
}
bool WolfsslCrypto::aes128CbcDecrypt(const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*) { return false; }
bool WolfsslCrypto::aes256CbcDecrypt(const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*) { return false; }
bool WolfsslCrypto::x25519MakeKey(uint8_t*, uint8_t*) { return false; }
bool WolfsslCrypto::x25519SharedSecret(const uint8_t*, const uint8_t*, uint8_t*) { return false; }
bool WolfsslCrypto::hkdfSha256(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t) { return false; }
bool WolfsslCrypto::aes256GcmDecrypt(const uint8_t*, const uint8_t*, const uint8_t*, size_t, const uint8_t*, uint8_t*) {
  return false;
}
void WolfsslCrypto::sha1(const uint8_t* data, size_t len, uint8_t out[20]) { ::sha1(data, len, out); }
void WolfsslCrypto::sha256(const uint8_t* data, size_t len, uint8_t out[32]) { ::sha256(data, len, out); }
bool WolfsslCrypto::rsaGenerate(RsaKeyPairDer*) { return false; }
bool WolfsslCrypto::rsaPublicEncrypt(const uint8_t*, size_t, const uint8_t*, size_t, uint8_t*, size_t, size_t*) {
  return false;
}
bool WolfsslCrypto::rsaPrivateSignRaw(const uint8_t*, size_t, const uint8_t*, uint8_t*) { return false; }
bool WolfsslCrypto::aes128CbcEncrypt(const uint8_t*, const uint8_t*, const uint8_t*, size_t, uint8_t*) { return false; }
bool WolfsslCrypto::pkcs12Extract(const uint8_t*, size_t, const std::string&, std::vector<uint8_t>*,
                                  std::vector<uint8_t>*) {
  return false;
}
void WolfsslCrypto::randomBytes(uint8_t* out, size_t len) { esp_fill_random(out, len); }

}  // namespace content
}  // namespace freeink
