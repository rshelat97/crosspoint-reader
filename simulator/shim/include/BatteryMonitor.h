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
};
