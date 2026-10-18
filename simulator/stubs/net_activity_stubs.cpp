// Simulator stub for Calibre Wireless, which needs a real LAN peer (the
// Calibre desktop app). Every other network activity is the REAL activity in
// the simulator, running against the fake-radio WiFi shim, the inert-TLS
// SecureNet client and the browser-bridged WebServer.
// Real headers, minimal lifecycle bodies: each shows a notice and backs out via
// the cancel path so every startActivityForResult caller takes its cancelled
// branch. Hardcoded strings are acceptable here: this file is simulator-only
// and never ships on device.
#include "activities/network/CalibreConnectActivity.h"
#include "components/UITheme.h"

namespace {
void renderUnavailable(GfxRenderer& renderer, const char* what) {
  renderer.clearScreen();
  GUI.drawPopup(renderer, what);
  renderer.displayBuffer();
}
bool backOrConfirmReleased(MappedInputManager& input) {
  return input.wasReleased(MappedInputManager::Button::Back) || input.wasReleased(MappedInputManager::Button::Confirm);
}
}  // namespace

// --- CalibreConnectActivity --------------------------------------------------
void CalibreConnectActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}
void CalibreConnectActivity::onExit() { Activity::onExit(); }
void CalibreConnectActivity::loop() {
  if (backOrConfirmReleased(mappedInput)) onGoHome();
}
void CalibreConnectActivity::render(RenderLock&&) {
  renderUnavailable(renderer, "Calibre connect is not available in the simulator");
}
