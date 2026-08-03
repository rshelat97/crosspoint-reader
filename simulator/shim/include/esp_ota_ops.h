#pragma once
// Simulator shim: OTA slot bookkeeping does not exist on the host.
#include "esp_err.h"
#include "esp_partition.h"

inline const esp_partition_t* esp_ota_get_running_partition() { return nullptr; }
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return nullptr; }
inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) { return ESP_FAIL; }
inline const esp_partition_t* esp_ota_get_boot_partition() { return nullptr; }
