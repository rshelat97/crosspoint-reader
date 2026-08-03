// Simulator implementation of HalGPIO: same header, input state supplied by
// the simulator input engine (keyboard/mouse events from the frontend).
#include <HalGPIO.h>

#include "../sim/sim_input.h"

HalGPIO gpio;

void HalGPIO::begin() {}
void HalGPIO::update() { siminput::update(); }

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return siminput::isPressed(buttonIndex); }
bool HalGPIO::wasPressed(uint8_t buttonIndex) const { return siminput::wasPressed(buttonIndex); }
bool HalGPIO::wasAnyPressed() const { return siminput::wasAnyPressed(); }
bool HalGPIO::wasReleased(uint8_t buttonIndex) const { return siminput::wasReleased(buttonIndex); }
bool HalGPIO::wasAnyReleased() const { return siminput::wasAnyReleased(); }
unsigned long HalGPIO::getHeldTime() const { return siminput::heldTimeMs(); }
unsigned long HalGPIO::getPowerButtonHeldTime() const { return siminput::powerHeldTimeMs(); }

bool HalGPIO::hasTouch() const { return siminput::hasTouchHardware(); }
bool HalGPIO::wasTouchTap(float& nx, float& ny) const { return siminput::wasTouchTap(nx, ny); }
bool HalGPIO::wasTouchDown(float& nx, float& ny) const { return siminput::wasTouchDownEvent(nx, ny); }
bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  return siminput::isTouchTapCandidate(nx, ny, heldMs);
}
bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return siminput::isTouchHeldAt(nx, ny); }
unsigned long HalGPIO::lastTouchHeldMs() const { return siminput::lastTouchHeldMs(); }
bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  return siminput::wasSwipe(nxStart, nyStart, nxEnd, nyEnd);
}
bool HalGPIO::wasTouchActivity() const { return siminput::wasTouchActivity(); }

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(bool) {}

bool HalGPIO::verifyPowerButtonWakeup(uint16_t, bool) { return true; }

bool HalGPIO::isUsbConnected() const { return false; }
bool HalGPIO::wasUsbStateChanged() const { return false; }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const { return WakeupReason::AfterFlash; }

bool HalGPIO::isXteinkDevice() const { return true; }
