// Simulator stubs for the five network-dependent activities. Real headers,
// minimal lifecycle bodies: each shows a notice and backs out via the cancel
// path so every startActivityForResult caller takes its cancelled branch.
// Hardcoded strings are acceptable here: this file is simulator-only and
// never ships on device.
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/network/CalibreConnectActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/network/WifiSelectionActivity.h"
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

// --- WifiSelectionActivity ---------------------------------------------------
void WifiSelectionActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}
void WifiSelectionActivity::onExit() { Activity::onExit(); }
void WifiSelectionActivity::loop() {
  if (backOrConfirmReleased(mappedInput)) {
    ActivityResult result;
    result.isCancelled = true;
    result.data = WifiResult{};
    setResult(std::move(result));
    finish();
  }
}
void WifiSelectionActivity::render(RenderLock&&) {
  renderUnavailable(renderer, "Wi-Fi is not available in the simulator");
}

// --- CrossPointWebServerActivity --------------------------------------------
void CrossPointWebServerActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}
void CrossPointWebServerActivity::onExit() { Activity::onExit(); }
void CrossPointWebServerActivity::loop() {
  if (backOrConfirmReleased(mappedInput)) onGoHome();
}
void CrossPointWebServerActivity::render(RenderLock&&) {
  renderUnavailable(renderer, "File transfer is not available in the simulator");
}

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
