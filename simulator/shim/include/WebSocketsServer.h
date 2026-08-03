#pragma once
// Simulator shim: no WebSocket clients ever connect, so the Files page's
// uploader falls back to its HTTP path (which the WebServer shim supports).
#include <cstdint>
#include <functional>

#include "WString.h"

typedef enum {
  WStype_ERROR = 0,
  WStype_DISCONNECTED,
  WStype_CONNECTED,
  WStype_TEXT,
  WStype_BIN,
  WStype_FRAGMENT_TEXT_START,
  WStype_FRAGMENT_BIN_START,
  WStype_FRAGMENT,
  WStype_FIN,
  WStype_PING,
  WStype_PONG,
} WStype_t;

class WebSocketsServer {
 public:
  typedef std::function<void(uint8_t num, WStype_t type, uint8_t* payload, size_t length)> WebSocketServerEvent;

  explicit WebSocketsServer(uint16_t = 81) {}
  void begin() {}
  void close() {}
  void loop() {}
  void onEvent(WebSocketServerEvent) {}
  bool sendTXT(uint8_t, const char*) { return true; }
  bool sendTXT(uint8_t, const String&) { return true; }
  void disconnect(uint8_t) {}
};
