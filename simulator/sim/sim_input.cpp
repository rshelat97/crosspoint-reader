#include "sim_input.h"

#include <cmath>
#include <vector>

unsigned long millis();  // from the Arduino shim

namespace siminput {
namespace {

// Thresholds mirror freeink InputManager (panel-native px / ms).
constexpr float PANEL_W = 800.0f;
constexpr float PANEL_H = 480.0f;
constexpr float TAP_SLOP_PX = 28.0f;
constexpr float SWIPE_MIN_PX = 60.0f;
constexpr unsigned long SWIPE_MAX_MS = 700;

struct TouchPoint {
  float nx = 0, ny = 0;
  unsigned long timestamp = 0;
};

struct PendingEvent {
  enum Type : uint8_t { ButtonDown, ButtonUp, TouchDown, TouchMove, TouchUp } type;
  uint8_t btn = 0;
  float nx = 0, ny = 0;
};

std::vector<PendingEvent> pendingEvents;

// Committed (frame-scoped) state.
uint8_t currentState = 0;
uint8_t pressedEvents = 0;
uint8_t releasedEvents = 0;
unsigned long buttonPressStart = 0;
unsigned long buttonPressFinish = 0;
unsigned long powerPressStart = 0;
unsigned long powerPressFinish = 0;

bool touchContact = false;  // live level between frames
bool touchPressed = false;  // frame-committed level
bool touchPressedEvent = false;
bool touchReleasedEvent = false;
bool movedBeyondSlop = false;
TouchPoint touchDownPoint;
TouchPoint touchLivePoint;
unsigned long lastTouchHeldDurationMs = 0;

float nativeDeltaPx(const TouchPoint& a, const TouchPoint& b, bool xAxis) {
  return xAxis ? (b.nx - a.nx) * PANEL_W : (b.ny - a.ny) * PANEL_H;
}

}  // namespace

void injectButton(uint8_t btnIndex, bool down) {
  if (btnIndex > 6) return;
  pendingEvents.push_back({down ? PendingEvent::ButtonDown : PendingEvent::ButtonUp, btnIndex, 0, 0});
}
void injectTouchDown(float nx, float ny) { pendingEvents.push_back({PendingEvent::TouchDown, 0, nx, ny}); }
void injectTouchMove(float nx, float ny) { pendingEvents.push_back({PendingEvent::TouchMove, 0, nx, ny}); }
void injectTouchUp(float nx, float ny) { pendingEvents.push_back({PendingEvent::TouchUp, 0, nx, ny}); }

void update() {
  const unsigned long now = millis();
  const uint8_t prevState = currentState;
  touchPressedEvent = false;
  touchReleasedEvent = false;

  for (const auto& ev : pendingEvents) {
    switch (ev.type) {
      case PendingEvent::ButtonDown:
        currentState |= static_cast<uint8_t>(1u << ev.btn);
        break;
      case PendingEvent::ButtonUp:
        currentState &= static_cast<uint8_t>(~(1u << ev.btn));
        break;
      case PendingEvent::TouchDown:
        touchContact = true;
        touchPressedEvent = true;
        touchDownPoint = {ev.nx, ev.ny, now};
        touchLivePoint = touchDownPoint;
        movedBeyondSlop = false;
        break;
      case PendingEvent::TouchMove:
        if (touchContact) {
          touchLivePoint = {ev.nx, ev.ny, now};
          const float dx = nativeDeltaPx(touchDownPoint, touchLivePoint, true);
          const float dy = nativeDeltaPx(touchDownPoint, touchLivePoint, false);
          if (std::fabs(dx) > TAP_SLOP_PX || std::fabs(dy) > TAP_SLOP_PX) movedBeyondSlop = true;
        }
        break;
      case PendingEvent::TouchUp:
        if (touchContact) {
          touchContact = false;
          touchReleasedEvent = true;
          touchLivePoint = {ev.nx, ev.ny, now};
          lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
        }
        break;
    }
  }
  pendingEvents.clear();

  touchPressed = touchContact || touchReleasedEvent;

  // Edge masks, exactly InputManager::applyStateChange semantics.
  pressedEvents = static_cast<uint8_t>(currentState & ~prevState);
  releasedEvents = static_cast<uint8_t>(prevState & ~currentState);

  // Hold timing: starts on the first button of a chord, finishes when all release.
  if (prevState == 0 && currentState != 0) buttonPressStart = now;
  if (prevState != 0 && currentState == 0) buttonPressFinish = now;
  const uint8_t powerBit = 1u << 6;
  if (!(prevState & powerBit) && (currentState & powerBit)) powerPressStart = now;
  if ((prevState & powerBit) && !(currentState & powerBit)) powerPressFinish = now;
}

bool isPressed(uint8_t b) { return (currentState >> b) & 1u; }
bool wasPressed(uint8_t b) { return (pressedEvents >> b) & 1u; }
bool wasReleased(uint8_t b) { return (releasedEvents >> b) & 1u; }
bool wasAnyPressed() { return pressedEvents != 0; }
bool wasAnyReleased() { return releasedEvents != 0; }

unsigned long heldTimeMs() {
  if (currentState != 0) return millis() - buttonPressStart;
  return buttonPressFinish - buttonPressStart;
}
unsigned long powerHeldTimeMs() {
  if (isPressed(6)) return millis() - powerPressStart;
  return powerPressFinish - powerPressStart;
}

bool hasTouchHardware() { return true; }

bool wasTouchTap(float& nx, float& ny) {
  if (!(touchReleasedEvent && !movedBeyondSlop)) return false;
  nx = touchDownPoint.nx;  // deliberate: tap reports the touch-DOWN point
  ny = touchDownPoint.ny;
  return true;
}
bool wasTouchDownEvent(float& nx, float& ny) {
  if (!touchPressedEvent) return false;
  nx = touchDownPoint.nx;
  ny = touchDownPoint.ny;
  return true;
}
bool isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) {
  if (!(touchContact && !movedBeyondSlop)) return false;
  nx = touchDownPoint.nx;
  ny = touchDownPoint.ny;
  heldMs = millis() - touchDownPoint.timestamp;
  return true;
}
bool isTouchHeldAt(float& nx, float& ny) {
  if (!touchContact) return false;
  nx = touchLivePoint.nx;  // live finger position, no slop gate
  ny = touchLivePoint.ny;
  return true;
}
unsigned long lastTouchHeldMs() { return lastTouchHeldDurationMs; }

bool wasSwipe(float& nxS, float& nyS, float& nxE, float& nyE) {
  if (!touchReleasedEvent) return false;
  if (lastTouchHeldDurationMs > SWIPE_MAX_MS) return false;
  const float dx = nativeDeltaPx(touchDownPoint, touchLivePoint, true);
  const float dy = nativeDeltaPx(touchDownPoint, touchLivePoint, false);
  if (std::fabs(dx) < SWIPE_MIN_PX && std::fabs(dy) < SWIPE_MIN_PX) return false;
  nxS = touchDownPoint.nx;
  nyS = touchDownPoint.ny;
  nxE = touchLivePoint.nx;
  nyE = touchLivePoint.ny;
  return true;
}
bool wasTouchActivity() { return touchPressedEvent || touchReleasedEvent; }

}  // namespace siminput
