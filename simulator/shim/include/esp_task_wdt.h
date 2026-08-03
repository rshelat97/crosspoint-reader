#pragma once
// Simulator shim: task watchdog is absent on the host.
#include "esp_err.h"

inline esp_err_t esp_task_wdt_status(void*) { return ESP_ERR_NOT_FOUND; }
inline esp_err_t esp_task_wdt_reset() { return ESP_OK; }
inline esp_err_t esp_task_wdt_add(void*) { return ESP_OK; }
inline esp_err_t esp_task_wdt_delete(void*) { return ESP_OK; }
