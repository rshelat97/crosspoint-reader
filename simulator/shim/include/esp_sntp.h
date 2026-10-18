#pragma once
// Simulator shim: ESP-IDF SNTP. There is no network, so a sync never
// completes; the host clock is already correct, which is what a successful
// sync would have achieved.
#include <cstdlib>
#include <ctime>

typedef enum {
  SNTP_SYNC_STATUS_RESET = 0,
  SNTP_SYNC_STATUS_COMPLETED,
  SNTP_SYNC_STATUS_IN_PROGRESS
} sntp_sync_status_t;
typedef void (*sntp_sync_time_cb_t)(struct timeval* tv);

inline void configTzTime(const char* tz, const char* = nullptr, const char* = nullptr, const char* = nullptr) {
  if (tz) {
    setenv("TZ", tz, 1);
    tzset();
  }
}
inline sntp_sync_status_t sntp_get_sync_status() { return SNTP_SYNC_STATUS_RESET; }
inline void sntp_set_time_sync_notification_cb(sntp_sync_time_cb_t) {}
inline bool esp_sntp_enabled() { return false; }
inline void esp_sntp_stop() {}
