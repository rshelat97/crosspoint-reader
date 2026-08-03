#pragma once
// Simulator shim: efuse reads report a fixed simulator serial number.
#include <cstring>

#include "esp_err.h"

typedef struct {
  int dummy;
} esp_efuse_desc_t;

inline esp_err_t esp_efuse_read_field_blob(const esp_efuse_desc_t* const*, void* dst, size_t dstSizeBits) {
  const char* serial = "CPSIM-0001";
  const size_t bytes = dstSizeBits / 8;
  memset(dst, 0, bytes);
  strncpy(static_cast<char*>(dst), serial, bytes > 0 ? bytes - 1 : 0);
  return ESP_OK;
}
