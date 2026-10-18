#pragma once
// Simulator shim for the slice of wolfCrypt the firmware links against.
// Hashes are real (stubs/crypto_stubs.cpp); ciphers and RSA report failure —
// DRM-protected (LCP) books and the account plugin's device crypto are not
// simulated. Unprotected books, which is everything else, are unaffected.
#include <cstddef>
#include <cstdint>

typedef uint8_t byte;
typedef uint32_t word32;
#define INVALID_DEVID (-2)
#define WC_SHA256_DIGEST_SIZE 32
#define WC_SHA_DIGEST_SIZE 20
#define BAD_FUNC_ARG (-173)
#define NOT_COMPILED_IN (-174)
