#pragma once
// Simulator shim: minimal Arduino Stream (only referenced as a type in
// network-facing signatures that the simulator stubs out).
#include "Print.h"

class Stream : public Print {
 public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
};
