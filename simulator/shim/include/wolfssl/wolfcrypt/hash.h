#pragma once
#include "types.h"
int wc_Sha256Hash(const byte* data, word32 len, byte* hash);
int wc_ShaHash(const byte* data, word32 len, byte* hash);
