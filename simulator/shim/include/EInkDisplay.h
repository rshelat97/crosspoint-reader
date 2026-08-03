#pragma once
// Simulator shim for freeink's EInkDisplay. The sim's HalDisplay.cpp
// implements the display on top of this in-memory framebuffer; the class
// mirrors only what lib/hal/HalDisplay.h needs to compile (dimension
// constants and a by-value member with the 6-pin constructor).
#include <cstdint>
#include <cstring>

class EInkDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = static_cast<uint32_t>(DISPLAY_WIDTH_BYTES) * DISPLAY_HEIGHT;

  enum RefreshMode { FULL, HALF, FAST };
  enum GrayPlane { GRAY_PLANE_LSB = 0, GRAY_PLANE_MSB = 1 };

  EInkDisplay(int, int, int, int, int, int) {}

  // Const-qualified because HalDisplay's const drawing methods mutate pixels;
  // the buffer is the display's mutable output surface, not logical state.
  uint8_t* frameBuffer() const { return buffer_; }

 private:
  mutable uint8_t buffer_[BUFFER_SIZE] = {};
};

namespace freeink {
using FreeInkDisplay = ::EInkDisplay;
}
