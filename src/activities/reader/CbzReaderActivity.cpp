#include "CbzReaderActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "components/UITheme.h"
#include "fontIds.h"

bool CbzReaderActivity::loadBook() {
  auto loaded = makeUniqueNoThrow<Cbz>(bookPath);
  if (!loaded) {
    LOG_ERR("CBR", "OOM: CBZ object");
    return false;
  }
  if (!loaded->load()) {
    LOG_ERR("CBR", "Failed to load CBZ");
    return false;
  }
  cbz = std::move(loaded);
  cbz->setupCacheDir();
  loadProgress();
  return true;
}

void CbzReaderActivity::onEnter() {
  ReaderActivity::onEnter();
  if (!cbz) return;  // loadBook() failed; the base class is finishing this activity

  const uint16_t panelWidth = renderer.getDisplayWidth();
  const uint16_t panelHeight = renderer.getDisplayHeight();
  // Largest plane tile over both orientation families (3.8 KB portrait, 6.4 KB
  // landscape on the X4), so the buffer fits whatever orientation is active.
  gray16::PageSpec spec;
  spec.panelWidth = panelWidth;
  spec.panelHeight = panelHeight;
  for (const uint8_t orientation : {gray16::Portrait, gray16::LandscapeCounterClockwise}) {
    spec.orientation = orientation;
    gray16::PageSpec::logicalSize(orientation, panelWidth, panelHeight, &spec.pageWidth, &spec.pageHeight);
    tileScratchLen = std::max<size_t>(tileScratchLen, spec.maxTileBytes());
  }
  tileScratch = makeUniqueNoThrow<uint8_t[]>(tileScratchLen);
  if (!tileScratch) LOG_ERR("CBR", "OOM: plane tile (%u bytes)", static_cast<unsigned>(tileScratchLen));

  if (!display.supportsGray16()) {
    // The 4-level overlay needs plane 2 beside the framebuffer's plane 3.
    const size_t planeBytes = renderer.getBufferSize();
    planeScratch = makeUniqueNoThrow<uint8_t[]>(planeBytes);
    if (!planeScratch) LOG_ERR("CBR", "OOM: overlay plane (%u bytes)", static_cast<unsigned>(planeBytes));
  }
}

void CbzReaderActivity::onExit() {
  planeScratch.reset();
  tileScratch.reset();
  tileScratchLen = 0;
  ReaderActivity::onExit();
}

void CbzReaderActivity::renderBook() {
  if (!cbz) return;
  renderPage();
  saveProgress();
}

bool CbzReaderActivity::statusBarLayout(int* clearY, int* clearHeight, int* paddingBottom) const {
  const auto sb = SETTINGS.statusBarSpec();
  if (sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_HIDE) return false;
  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight <= 0) return false;

  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  const int screenHeight = renderer.getScreenHeight();
  if (sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP) {
    *clearY = marginTop;
    *clearHeight = statusBarHeight + 4;
    *paddingBottom = screenHeight - statusBarHeight - marginBottom - marginTop - 4;
  } else {
    *clearY = std::max(0, screenHeight - marginBottom - statusBarHeight - 4);
    *clearHeight = screenHeight - marginBottom - *clearY;
    *paddingBottom = 0;
  }
  return true;
}

// White box with black text, drawn identically into every plane so the bar
// reads as pure black and white in the final image.
void CbzReaderActivity::renderStatusBarOverlay() const {
  int clearY, clearHeight, paddingBottom;
  if (!statusBarLayout(&clearY, &clearHeight, &paddingBottom)) return;
  if (clearHeight > 0) renderer.fillRect(0, clearY, renderer.getScreenWidth(), clearHeight, false);

  const int pageCount = static_cast<int>(cbz->getPageCount());
  const int displayPage = static_cast<int>(currentPage) + 1;
  const float progress = pageCount > 0 ? (static_cast<float>(displayPage) * 100.0f) / pageCount : 0.0f;
  const auto sb = SETTINGS.statusBarSpec();
  std::string title = sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE ? cbz->getTitle() : "";
  GUI.drawStatusBar(renderer, progress, displayPage, pageCount, std::move(title), paddingBottom);
}

// The bar is pure B/W in the base frame: an overlay mask must not mark it.
void CbzReaderActivity::clearStatusBarMask() const {
  int clearY, clearHeight, paddingBottom;
  if (!statusBarLayout(&clearY, &clearHeight, &paddingBottom)) return;
  if (clearHeight > 0) renderer.fillRect(0, clearY, renderer.getScreenWidth(), clearHeight, true);
}

bool CbzReaderActivity::loadPlaneWithStatusBar(const uint8_t plane, uint8_t* framebuffer) const {
  if (!cbz->loadPlane(currentPage, plane, framebuffer, tileScratch.get(), tileScratchLen)) return false;
  renderStatusBarOverlay();
  return true;
}

void CbzReaderActivity::renderErrorPage(const char* message) const {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 300, message, true, EpdFontFamily::BOLD);
  renderer.displayBuffer();
}

void CbzReaderActivity::renderPage() {
  if (!tileScratch) {
    renderErrorPage(tr(STR_MEMORY_ERROR));
    return;
  }
  const auto orientation = static_cast<uint8_t>(renderer.getOrientation());
  const uint16_t panelWidth = renderer.getDisplayWidth();
  const uint16_t panelHeight = renderer.getDisplayHeight();
  if (!cbz->hasPagePlanes(currentPage, orientation, panelWidth, panelHeight)) {
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));  // displays the buffer itself
    if (!cbz->ensurePagePlanes(currentPage, orientation, panelWidth, panelHeight)) {
      LOG_ERR("CBR", "Failed to prepare page %u", static_cast<unsigned>(currentPage + 1));
      renderErrorPage(tr(STR_PAGE_LOAD_ERROR));
      return;
    }
  }

  uint8_t* framebuffer = renderer.getWriteTarget();
  const bool rendered = display.beginGray16() ? renderGray16(framebuffer) : renderGray4(framebuffer);
  if (!rendered) {
    renderErrorPage(tr(STR_PAGE_LOAD_ERROR));
    return;
  }
  LOG_DBG("CBR", "Rendered page %u/%u", static_cast<unsigned>(currentPage + 1),
          static_cast<unsigned>(cbz->getPageCount()));
  markPageRendered();
}

// Two absolute passes: coarse (planes 2, 3) then fine (planes 0, 1), each
// uploaded as LSB/MSB. Called after display.beginGray16() returned true and
// always ends the sequence, even on a plane read failure.
bool CbzReaderActivity::renderGray16(uint8_t* framebuffer) {
  bool ok = true;
  for (uint8_t pass = 0; pass < 2 && ok; pass++) {
    const uint8_t low = pass == 0 ? 2 : 0;
    ok = loadPlaneWithStatusBar(low, framebuffer);
    if (!ok) break;
    renderer.copyGrayscaleLsbBuffers();
    ok = loadPlaneWithStatusBar(low + 1, framebuffer);
    if (!ok) break;
    renderer.copyGrayscaleMsbBuffers();
    display.displayGray16Pass(pass);
  }
  // Leave the framebuffer holding the B/W approximation (plane 3): it is the
  // controller's differential baseline for the next ordinary refresh.
  if (ok) ok = loadPlaneWithStatusBar(3, framebuffer);
  if (!ok) renderer.clearScreen();
  display.endGray16(framebuffer);
  renderer.cleanupGrayscaleWithFrameBuffer();
  return ok;
}

// 4-level overlay, same sequence as the XTCH reader: base = plane 3, then the
// masks MSB = plane3 ^ plane2 (dark or light) and LSB = ~plane3 & plane2
// (dark), computed in place in the framebuffer against plane 2 in the scratch.
bool CbzReaderActivity::renderGray4(uint8_t* framebuffer) {
  if (!planeScratch) {
    LOG_ERR("CBR", "No overlay plane buffer");
    return false;
  }
  if (!loadPlaneWithStatusBar(3, framebuffer)) return false;

  if (pagesUntilFullRefresh <= 1) {
    // Periodic ghost cleanup; combined-base panels defer the base so the gray
    // planes join it in one waveform.
    if (renderer.combinesGrayscaleBase()) {
      renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      renderer.preconditionGrayscale();
    }
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
    pagesUntilFullRefresh--;
  }

  uint8_t* plane2 = planeScratch.get();
  if (!cbz->loadPlane(currentPage, 2, plane2, tileScratch.get(), tileScratchLen)) return false;
  const size_t planeBytes = renderer.getBufferSize();
  for (size_t i = 0; i < planeBytes; i++) framebuffer[i] ^= plane2[i];
  clearStatusBarMask();
  renderer.copyGrayscaleMsbBuffers();
  for (size_t i = 0; i < planeBytes; i++) framebuffer[i] &= plane2[i];
  renderer.copyGrayscaleLsbBuffers();
  renderer.displayGrayBuffer();

  if (!loadPlaneWithStatusBar(3, framebuffer)) return false;
  renderer.cleanupGrayscaleWithFrameBuffer();
  return true;
}

bool CbzReaderActivity::pageTurn(const bool isForward) {
  if (!cbz) return false;
  if (isForward) {
    if (currentPage < cbz->getPageCount()) {
      currentPage++;
      return true;
    }
  } else if (currentPage > 0) {
    currentPage--;
    return true;
  }
  return false;
}

bool CbzReaderActivity::skipPages(const int amount) {
  if (!cbz) return false;
  const int pageCount = static_cast<int>(cbz->getPageCount());
  const int newPage = std::clamp(static_cast<int>(currentPage) + amount, 0, pageCount);
  if (newPage == static_cast<int>(currentPage)) return false;
  currentPage = static_cast<uint32_t>(newPage);
  return true;
}

bool CbzReaderActivity::isAtEndOfBook() const { return cbz && currentPage >= cbz->getPageCount(); }

void CbzReaderActivity::onReturnFromEndOfBook() {
  currentPage = (cbz && cbz->getPageCount() > 0) ? cbz->getPageCount() - 1 : 0;
}

void CbzReaderActivity::saveProgress() {
  if (!cbz || currentPage == savedPage) return;
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = (currentPage >> 16) & 0xFF;
  data[3] = (currentPage >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(cbz->getCachePath(), data, sizeof(data))) {
    LOG_ERR("CBR", "Failed to save progress: page %u", static_cast<unsigned>(currentPage));
    return;
  }
  savedPage = currentPage;
}

void CbzReaderActivity::loadProgress() {
  if (!cbz) return;
  HalFile f;
  if (!Storage.openFileForRead("CBR", cbz->getCachePath() + "/progress.bin", f)) return;
  uint8_t data[4];
  if (f.read(data, 4) != 4) return;
  currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (static_cast<uint32_t>(data[3]) << 24);
  if (currentPage >= cbz->getPageCount() && cbz->getPageCount() > 0) {
    currentPage = cbz->getPageCount() - 1;
  }
  savedPage = currentPage;
  LOG_DBG("CBR", "Loaded progress: page %u/%u", static_cast<unsigned>(currentPage + 1),
          static_cast<unsigned>(cbz->getPageCount()));
}

ScreenshotInfo CbzReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Cbz;
  if (cbz) {
    const std::string t = cbz->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = cbz->getPageCount();
    info.totalPages = static_cast<int>(pageCount);
    const uint32_t clampedPage = (pageCount > 0 && currentPage >= pageCount) ? pageCount - 1 : currentPage;
    info.progressPercent = pageCount > 0 ? cbz->calculateProgress(clampedPage) : 0;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = static_cast<int>(currentPage) + 1;
  }
  return info;
}
