#pragma once
// Simulator shim: field descriptor consumed by esp_efuse_read_field_blob.
#include "esp_efuse.h"

inline const esp_efuse_desc_t* const ESP_EFUSE_USER_DATA[] = {nullptr};
