#pragma once
// Simulator shim: OTA partitions do not exist on the host; the firmware
// flasher stubs return failure. Types exist so headers parse.
#include <cstddef>
#include <cstdint>

#include "esp_err.h"

typedef enum { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1 } esp_partition_type_t;
typedef enum {
  ESP_PARTITION_SUBTYPE_APP_FACTORY = 0,
  ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10,
  ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11,
  ESP_PARTITION_SUBTYPE_DATA_OTA = 0,
  ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;

typedef struct {
  esp_partition_type_t type;
  esp_partition_subtype_t subtype;
  uint32_t address;
  uint32_t size;
  char label[17];
} esp_partition_t;

inline const esp_partition_t* esp_partition_find_first(esp_partition_type_t, esp_partition_subtype_t, const char*) {
  return nullptr;
}
inline esp_err_t esp_partition_read(const esp_partition_t*, size_t, void*, size_t) { return ESP_FAIL; }
inline esp_err_t esp_partition_write(const esp_partition_t*, size_t, const void*, size_t) { return ESP_FAIL; }
inline esp_err_t esp_partition_erase_range(const esp_partition_t*, size_t, size_t) { return ESP_FAIL; }
