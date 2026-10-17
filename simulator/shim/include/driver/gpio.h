#pragma once
// Simulator shim: ESP-IDF GPIO driver. No pins exist; holds/levels are no-ops.
#include <cstdint>

typedef int gpio_num_t;
#define GPIO_NUM_NC (-1)
#define GPIO_NUM_0 0
#define GPIO_NUM_5 5
#define GPIO_NUM_13 13
typedef enum { GPIO_MODE_DISABLE = 0, GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_MODE_INPUT_OUTPUT } gpio_mode_t;

inline int gpio_hold_en(gpio_num_t) { return 0; }
inline int gpio_hold_dis(gpio_num_t) { return 0; }
inline void gpio_deep_sleep_hold_en() {}
inline void gpio_deep_sleep_hold_dis() {}
inline int gpio_set_direction(gpio_num_t, gpio_mode_t) { return 0; }
inline int gpio_set_level(gpio_num_t, uint32_t) { return 0; }
inline int gpio_get_level(gpio_num_t) { return 0; }
inline int gpio_reset_pin(gpio_num_t) { return 0; }
