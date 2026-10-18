// Simulator implementation of HalGPIO: same header, input state supplied by
// the simulator input engine (keyboard/mouse events from the frontend).
#include <BoardConfig.h>
#include <HalGPIO.h>

#include "../sim/sim_input.h"

HalGPIO gpio;

void HalGPIO::begin() {}
void HalGPIO::prepareForDeepSleep() {}
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
bool HalGPIO::wasTouchReleased() const { return siminput::wasTouchReleased(); }
bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const { return siminput::wasTouchLongPress(nx, ny); }
void HalGPIO::suppressTouchContact() { siminput::suppressTouchContact(); }
bool HalGPIO::rawInputActive() { return siminput::anyInputActive(); }

// Board-keyed capabilities, exactly as the device HalGPIO derives them: the X4
// has a vertical side rocker (not edge buttons) and no capacitive Home key.
bool HalGPIO::hasEdgeSideButtons() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Pro ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Classic;
}
bool HalGPIO::hasHomeKey() const { return BoardConfig::hasHomeKey(); }
bool HalGPIO::wasHomeKeyPressed() const { return false; }
bool HalGPIO::wasHomeKeyTapped() const { return false; }
bool HalGPIO::wasHomeKeyLongPressed() const { return false; }

void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(bool) {}

bool HalGPIO::verifyPowerButtonWakeup() { return true; }

bool HalGPIO::isUsbConnected() const { return false; }
bool HalGPIO::wasUsbStateChanged() const { return false; }

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const { return WakeupReason::AfterFlash; }

bool HalGPIO::isXteinkDevice() const { return true; }
