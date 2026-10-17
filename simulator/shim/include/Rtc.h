#pragma once
// Simulator shim for the SDK's Rtc: a hardware RTC backed by host wall-clock
// time. Like the real chip it keeps UTC — HalClock applies the timezone.
#include <cstdint>
#include <ctime>

namespace freeink {

class Rtc {
 public:
  struct DateTime {
    uint16_t year = 2000;
    uint8_t month = 1;  // 1-12
    uint8_t day = 1;    // 1-31
    uint8_t hour = 0;   // 0-23
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;  // 0=Sunday .. 6=Saturday
  };

  bool begin() {
    begun_ = true;
    return true;
  }
  bool present() const { return begun_; }

  bool now(DateTime& out) {
    const time_t t = time(nullptr) + offsetSeconds_;
    struct tm tmv;
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    out.year = static_cast<uint16_t>(tmv.tm_year + 1900);
    out.month = static_cast<uint8_t>(tmv.tm_mon + 1);
    out.day = static_cast<uint8_t>(tmv.tm_mday);
    out.hour = static_cast<uint8_t>(tmv.tm_hour);
    out.minute = static_cast<uint8_t>(tmv.tm_min);
    out.second = static_cast<uint8_t>(tmv.tm_sec);
    out.weekday = static_cast<uint8_t>(tmv.tm_wday);
    return true;
  }
  // The host clock is authoritative; setting the RTC is accepted and ignored.
  bool set(const DateTime&) { return true; }
  bool adjust(int32_t seconds, DateTime* out = nullptr) {
    offsetSeconds_ += seconds;
    return out ? now(*out) : true;
  }
  bool armWakeTimer(uint32_t) { return false; }
  bool cancelWakeTimer() { return true; }
  bool readWakeTimer(bool& fired) {
    fired = false;
    return true;
  }

 private:
  bool begun_ = false;
  int64_t offsetSeconds_ = 0;
};

}  // namespace freeink

using Rtc = freeink::Rtc;
