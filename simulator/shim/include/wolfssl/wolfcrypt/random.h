#pragma once
#include "types.h"
typedef struct WC_RNG {
  int unused;
} WC_RNG;
int wc_InitRng(WC_RNG* rng);
int wc_RNG_GenerateBlock(WC_RNG* rng, byte* out, word32 sz);
int wc_FreeRng(WC_RNG* rng);
