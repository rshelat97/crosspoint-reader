#pragma once
// Simulator shim: field descriptor consumed by esp_efuse_read_field_blob.
#include "esp_efuse.h"

inline const esp_efuse_desc_t* const ESP_EFUSE_USER_DATA[] = {nullptr};
inline const esp_efuse_desc_t* const ESP_EFUSE_DIS_DOWNLOAD_MODE[] = {nullptr};
inline const esp_efuse_desc_t* const ESP_EFUSE_DIS_USB_SERIAL_JTAG[] = {nullptr};
inline const esp_efuse_desc_t* const ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE[] = {nullptr};
inline const esp_efuse_desc_t* const ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD[] = {nullptr};
