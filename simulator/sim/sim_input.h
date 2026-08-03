#pragma once
// Simulator input engine. The host frontend (web JS or native test driver)
// feeds raw events in; the sim HalGPIO reads frame-scoped button edges and
// touch state out. Semantics mirror freeink's InputManager: edges are
// recomputed on each update() and stable within a frame; touch positions are
// normalized 0..1 in the panel-native (800x480) frame; tap slop, swipe
// distance and swipe duration use panel-native pixels/ms.
#include <cstdint>

namespace siminput {

// --- event injection (called from JS bindings / native driver) ---------------
void injectButton(uint8_t btnIndex, bool down);  // HalGPIO::BTN_* index
void injectTouchDown(float nx, float ny);
void injectTouchMove(float nx, float ny);
void injectTouchUp(float nx, float ny);

// --- per-frame commit (called from HalGPIO::update once per loop) ------------
void update();

// --- button state (frame-scoped, same answer for repeated calls) -------------
bool isPressed(uint8_t btnIndex);
bool wasPressed(uint8_t btnIndex);
bool wasReleased(uint8_t btnIndex);
bool wasAnyPressed();
bool wasAnyReleased();
unsigned long heldTimeMs();
unsigned long powerHeldTimeMs();

// --- touch state --------------------------------------------------------------
bool hasTouchHardware();
bool wasTouchTap(float& nx, float& ny);
bool wasTouchDownEvent(float& nx, float& ny);
bool isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs);
bool isTouchHeldAt(float& nx, float& ny);
unsigned long lastTouchHeldMs();
bool wasSwipe(float& nxS, float& nyS, float& nxE, float& nyE);
bool wasTouchActivity();

}  // namespace siminput
