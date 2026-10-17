#pragma once
#include "types.h"
enum { AES_ENCRYPTION = 0, AES_DECRYPTION = 1, AES_BLOCK_SIZE = 16 };
typedef struct Aes {
  int unused;
} Aes;
int wc_AesInit(Aes* aes, void* heap, int devId);
void wc_AesFree(Aes* aes);
int wc_AesSetKey(Aes* aes, const byte* key, word32 len, const byte* iv, int dir);
int wc_AesCbcEncrypt(Aes* aes, byte* out, const byte* in, word32 sz);
int wc_AesCbcDecrypt(Aes* aes, byte* out, const byte* in, word32 sz);
int wc_AesGcmSetKey(Aes* aes, const byte* key, word32 len);
int wc_AesGcmEncrypt(Aes* aes, byte* out, const byte* in, word32 sz, const byte* iv, word32 ivSz, byte* authTag,
                     word32 authTagSz, const byte* authIn, word32 authInSz);
int wc_AesGcmDecrypt(Aes* aes, byte* out, const byte* in, word32 sz, const byte* iv, word32 ivSz, const byte* authTag,
                     word32 authTagSz, const byte* authIn, word32 authInSz);
