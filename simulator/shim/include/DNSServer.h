#pragma once
// Simulator shim: captive-portal DNS is meaningless without a radio.
#include <cstdint>

#include "Arduino.h"

enum class DNSReplyCode : uint16_t {
  NoError = 0,
  FormError = 1,
  ServerFailure = 2,
  NonExistentDomain = 3,
  NotImplemented = 4,
  Refused = 5,
};

class DNSServer {
 public:
  bool start(uint16_t, const String&, IPAddress) { return true; }
  void stop() {}
  void processNextRequest() {}
  void setTTL(uint32_t) {}
  void setErrorReplyCode(DNSReplyCode) {}
};
