#pragma once
// Simulator shim: a full, happy battery.
#include <cstdint>

class BatteryMonitor {
 public:
  uint16_t readPercentage() const { return 87; }
  bool readPercentageChecked(uint16_t& out) const {
    out = 87;
    return true;
  }
  bool isCharging() const { return false; }
  // Fuel-gauge calibration (X3 only); nothing to load in the simulator.
  static bool loadDesignCapacity() { return false; }
  static bool enterShipMode() { return false; }
};
