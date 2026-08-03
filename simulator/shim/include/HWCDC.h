#pragma once
// Simulator shim: USB CDC serial is the same stdout-backed stream as
// HardwareSerial on the host.
#include "HardwareSerial.h"

using HWCDC = HardwareSerial;
extern HWCDC USBSerial;
