#pragma once
// Simulator shim: mDNS pretends to start; crosspoint.local is not a real name
// in the browser sandbox — the frontend bridge is the actual entry point.
#include "WString.h"

class MDNSResponder {
 public:
  bool begin(const char*) { return true; }
  void end() {}
  void addService(const char*, const char*, uint16_t) {}
};
extern MDNSResponder MDNS;
