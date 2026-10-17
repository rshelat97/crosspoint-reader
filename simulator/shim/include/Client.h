#pragma once
// Simulator shim for Arduino's Client: the abstract byte-stream connection
// that NetworkClient (WiFi.h) and SecureNet's SecureClient implement.
#include <cstddef>
#include <cstdint>

#include "Stream.h"

class IPAddress;

class Client : public Stream {
 public:
  virtual ~Client() = default;
  virtual int connect(IPAddress ip, uint16_t port) = 0;
  virtual int connect(const char* host, uint16_t port) = 0;
  size_t write(uint8_t) override = 0;
  size_t write(const uint8_t* buf, size_t size) override = 0;
  int available() override = 0;
  int read() override = 0;
  virtual int read(uint8_t* buf, size_t size) = 0;
  int peek() override = 0;
  void flush() override = 0;
  virtual void stop() = 0;
  virtual uint8_t connected() = 0;
  virtual operator bool() = 0;
};
