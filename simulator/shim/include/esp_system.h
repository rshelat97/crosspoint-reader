#pragma once
// Simulator shim: reset-reason surface consumed by HalGPIO/HalSystem sims.
#include <cstdint>

#include "esp_err.h"

typedef enum {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
  ESP_RST_CPU_LOCKUP,
} esp_reset_reason_t;

inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
[[noreturn]] void esp_restart();
