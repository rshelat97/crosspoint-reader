#pragma once
// Simulator shim: RTC backed by host wall-clock time.
#include <cstdint>
#include <ctime>

namespace freeink {

struct DateTime {
  uint16_t year = 2026;
  uint8_t month = 1, day = 1, hour = 0, minute = 0, second = 0;
};

class Rtc {
 public:
  bool begin() { return true; }
  bool now(DateTime& out) const {
    const time_t t = time(nullptr);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    out.year = static_cast<uint16_t>(tmv.tm_year + 1900);
    out.month = static_cast<uint8_t>(tmv.tm_mon + 1);
    out.day = static_cast<uint8_t>(tmv.tm_mday);
    out.hour = static_cast<uint8_t>(tmv.tm_hour);
    out.minute = static_cast<uint8_t>(tmv.tm_min);
    out.second = static_cast<uint8_t>(tmv.tm_sec);
    return true;
  }
  bool set(const DateTime&) { return true; }
};

}  // namespace freeink

using Rtc = freeink::Rtc;
using DateTime = freeink::DateTime;
