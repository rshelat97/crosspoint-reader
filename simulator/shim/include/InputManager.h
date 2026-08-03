#pragma once
// Simulator shim for freeink's InputManager. The sim's HalGPIO.cpp implements
// input directly from the host event queue (simulator/sim/SimInput), so this
// class only exists because lib/hal/HalGPIO.h holds one by value.
#include <cstdint>

class InputManager {
 public:
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;

  void begin() {}
  void update() {}
};
