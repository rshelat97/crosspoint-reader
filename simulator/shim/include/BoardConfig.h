#pragma once
// Simulator shim for freeink's BoardConfig: an X4-like profile with touch
// enabled (so touch UI paths are exercisable in the simulator).
#include <cstdint>

#define FREEINK_LOG_TRANSPORT_SERIAL 0
#define FREEINK_LOG_TRANSPORT_ROM_PRINTF 1
#ifndef FREEINK_LOG_TRANSPORT
#define FREEINK_LOG_TRANSPORT FREEINK_LOG_TRANSPORT_SERIAL
#endif

namespace BoardConfig {

enum class Board : uint8_t { XteinkX4 = 0, XteinkX3 = 1, Simulator = 99 };
enum class DisplayController : uint8_t { SSD1677 = 0, UC8253, ED2208, LgfxEpd, IT8951, UC8279, UC8179 };
enum class TouchController : uint8_t { None = 0, Chsc6x, Gt911 };

constexpr int8_t PIN_UNASSIGNED = -1;
constexpr uint32_t MAX_FRAMEBUFFER_BYTES = 52272;  // X3 panel, the largest supported

struct InputPins {
  int8_t back = PIN_UNASSIGNED, confirm = PIN_UNASSIGNED, left = PIN_UNASSIGNED, right = PIN_UNASSIGNED,
         up = PIN_UNASSIGNED, down = PIN_UNASSIGNED, power = PIN_UNASSIGNED;
};
struct BatteryGauge {
  uint8_t gaugeAddr = 0;
};
struct SdPins {
  int8_t cs = PIN_UNASSIGNED, sck = PIN_UNASSIGNED, mosi = PIN_UNASSIGNED, miso = PIN_UNASSIGNED;
};
struct PowerPins {
  int8_t latch0 = PIN_UNASSIGNED, latch1 = PIN_UNASSIGNED;
};
struct TouchConfig {
  TouchController controller = TouchController::Gt911;
  int16_t rawMinX = 0, rawMaxX = 800, rawMinY = 0, rawMaxY = 480;
  bool swapXY = false, flipX = false, flipY = false;
};
struct SensorConfig {
  bool hasImu = false;
};

struct BoardProfile {
  Board board = Board::XteinkX4;
  InputPins input;
  int8_t batteryAdc = PIN_UNASSIGNED;
  int8_t usbDetect = PIN_UNASSIGNED;
  BatteryGauge batteryGauge;
  SdPins sd;
  PowerPins power;
  TouchConfig touch;
  SensorConfig sensors;
  DisplayController displayController = DisplayController::SSD1677;
  uint8_t uiScale = 1;
};

inline BoardProfile ACTIVE{};
inline constexpr Board DEFAULT_DEVICE = Board::XteinkX4;

inline bool selectDevice(Board b) {
  ACTIVE.board = b;
  return true;
}
inline void holdPowerRails() {}
inline bool hasTouch() { return ACTIVE.touch.controller != TouchController::None; }

}  // namespace BoardConfig
