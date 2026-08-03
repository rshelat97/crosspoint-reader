#pragma once
// Simulator shim: NVS preferences as an in-memory map (per-run only; the HAL
// sim uses this solely for the device-type fingerprint cache).
#include <cstdint>
#include <map>
#include <string>

class Preferences {
 public:
  bool begin(const char* ns, bool = false) {
    ns_ = ns;
    return true;
  }
  void end() {}
  uint8_t getUChar(const char* key, uint8_t def = 0) {
    auto it = store().find(ns_ + "/" + key);
    return it == store().end() ? def : static_cast<uint8_t>(it->second);
  }
  size_t putUChar(const char* key, uint8_t value) {
    store()[ns_ + "/" + key] = value;
    return 1;
  }
  bool isKey(const char* key) { return store().count(ns_ + "/" + key) > 0; }
  bool remove(const char* key) { return store().erase(ns_ + "/" + key) > 0; }
  bool clear() {
    store().clear();
    return true;
  }

 private:
  static std::map<std::string, uint32_t>& store() {
    static std::map<std::string, uint32_t> s;
    return s;
  }
  std::string ns_;
};
