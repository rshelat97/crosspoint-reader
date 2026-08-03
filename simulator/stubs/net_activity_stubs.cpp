// Simulator stubs for the network activities that remain out of scope.
// (Wi-Fi selection and File Transfer are the REAL activities in the simulator,
// running against the fake-radio WiFi shim and the browser-bridged WebServer.)
// Real headers, minimal lifecycle bodies: each shows a notice and backs out via
// the cancel path so every startActivityForResult caller takes its cancelled
// branch. Hardcoded strings are acceptable here: this file is simulator-only
// and never ships on device.
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/network/CalibreConnectActivity.h"
#include "activities/reader/KOReaderSyncActivity.h"
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

// --- OpdsBookBrowserActivity -------------------------------------------------
void OpdsBookBrowserActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}
void OpdsBookBrowserActivity::onExit() { Activity::onExit(); }
void OpdsBookBrowserActivity::loop() {
  if (backOrConfirmReleased(mappedInput)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }
}
void OpdsBookBrowserActivity::render(RenderLock&&) {
  renderUnavailable(renderer, "OPDS browsing is not available in the simulator");
}

// --- KOReaderSyncActivity ----------------------------------------------------
void KOReaderSyncActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}
void KOReaderSyncActivity::onExit() { Activity::onExit(); }
void KOReaderSyncActivity::loop() {
  if (backOrConfirmReleased(mappedInput)) onGoHome();
}
void KOReaderSyncActivity::render(RenderLock&&) {
  renderUnavailable(renderer, "KOReader sync is not available in the simulator");
}
