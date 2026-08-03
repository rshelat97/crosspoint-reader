#pragma once
// Simulator shim for src/network/CrossPointWebServer.h. The web-server
// activities are stubbed in the simulator; their headers only need the type
// to exist with the members they reference inline (isRunning).
#include <cstddef>
#include <string>

class CrossPointWebServer {
 public:
  bool isRunning() const { return false; }
  size_t getUploadProgressReceived() const { return 0; }
  size_t getUploadProgressTotal() const { return 0; }
  std::string getCurrentUploadName() const { return {}; }
  std::string getLastCompleteName() const { return {}; }
  unsigned long getLastCompleteAt() const { return 0; }
};
