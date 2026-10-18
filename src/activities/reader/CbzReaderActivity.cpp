#include "CbzReaderActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Plane files bake the renderer's orientation value; gray16 mirrors the enum.
static_assert(gray16::Portrait == static_cast<uint8_t>(GfxRenderer::Portrait));
static_assert(gray16::LandscapeClockwise == static_cast<uint8_t>(GfxRenderer::LandscapeClockwise));
static_assert(gray16::PortraitInverted == static_cast<uint8_t>(GfxRenderer::PortraitInverted));
static_assert(gray16::LandscapeCounterClockwise == static_cast<uint8_t>(GfxRenderer::LandscapeCounterClockwise));

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
}

void CbzReaderActivity::onExit() {
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
  if (!cbz->loadPlane(currentPage, plane, renderer.getDisplayWidth(), renderer.getDisplayHeight(), framebuffer,
                      tileScratch.get(), tileScratchLen)) {
    return false;
  }
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
  const PageRender result = renderer.beginGray16() ? renderGray16(framebuffer) : renderGray4(framebuffer);
  if (result != PageRender::Ok) {
    renderErrorPage(result == PageRender::MemoryError ? tr(STR_MEMORY_ERROR) : tr(STR_PAGE_LOAD_ERROR));
    return;
  }
  LOG_DBG("CBR", "Rendered page %u/%u", static_cast<unsigned>(currentPage + 1),
          static_cast<unsigned>(cbz->getPageCount()));
  markPageRendered();
}

// Two absolute passes: coarse (planes 2, 3) then fine (planes 0, 1), each
// uploaded as LSB/MSB. Called after renderer.beginGray16() returned true and
// always ends the sequence, even on a plane read failure.
CbzReaderActivity::PageRender CbzReaderActivity::renderGray16(uint8_t* framebuffer) {
  bool ok = true;
  for (uint8_t pass = 0; pass < 2 && ok; pass++) {
    const uint8_t low = pass == 0 ? 2 : 0;
    ok = loadPlaneWithStatusBar(low, framebuffer);
    if (!ok) break;
    renderer.copyGrayscaleLsbBuffers();
    ok = loadPlaneWithStatusBar(low + 1, framebuffer);
    if (!ok) break;
    renderer.copyGrayscaleMsbBuffers();
    renderer.displayGray16Pass(pass);
  }
  // Leave the framebuffer holding the B/W approximation (plane 3): endGray16()
  // makes it the controller's differential baseline for the next refresh.
  if (ok) ok = loadPlaneWithStatusBar(3, framebuffer);
  if (!ok) renderer.clearScreen();
  renderer.endGray16();
  return ok ? PageRender::Ok : PageRender::LoadError;
}

// 4-level overlay, same sequence as the XTCH reader: base = plane 3, then the
// masks MSB = plane3 ^ plane2 (dark or light) and LSB = MSB & ~plane3 (dark),
// built in place in the framebuffer against a copy of the base.
CbzReaderActivity::PageRender CbzReaderActivity::renderGray4(uint8_t* framebuffer) {
  // The base (plane 3 with the status bar) must survive the mask arithmetic
  // and come back as the B/W baseline. Allocated per render, after the page
  // decode has released its buffers, and freed on return.
  const size_t planeBytes = renderer.getBufferSize();
  const auto base = makeUniqueNoThrow<uint8_t[]>(planeBytes);
  if (!base) {
    LOG_ERR("CBR", "OOM: overlay plane (%u bytes)", static_cast<unsigned>(planeBytes));
    return PageRender::MemoryError;
  }
  if (!loadPlaneWithStatusBar(3, framebuffer)) return PageRender::LoadError;

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

  memcpy(base.get(), framebuffer, planeBytes);
  if (!cbz->loadPlane(currentPage, 2, renderer.getDisplayWidth(), renderer.getDisplayHeight(), framebuffer,
                      tileScratch.get(), tileScratchLen)) {
    return PageRender::LoadError;
  }
  for (size_t i = 0; i < planeBytes; i++) framebuffer[i] ^= base[i];
  clearStatusBarMask();
  renderer.copyGrayscaleMsbBuffers();
  for (size_t i = 0; i < planeBytes; i++) framebuffer[i] &= static_cast<uint8_t>(~base[i]);
  renderer.copyGrayscaleLsbBuffers();
  renderer.displayGrayBuffer();

  memcpy(framebuffer, base.get(), planeBytes);  // plane 3 + status bar: the B/W baseline
  renderer.cleanupGrayscaleWithFrameBuffer();
  return PageRender::Ok;
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

// progress.bin: page index then page count, both little-endian uint32; the
// count lets the home screen show progress without opening the archive.
void CbzReaderActivity::saveProgress() {
  if (!cbz || currentPage == savedPage) return;
  uint8_t data[8];
  const uint32_t values[2] = {currentPage, cbz->getPageCount()};
  for (int i = 0; i < 2; i++) {
    data[i * 4] = values[i] & 0xFF;
    data[i * 4 + 1] = (values[i] >> 8) & 0xFF;
    data[i * 4 + 2] = (values[i] >> 16) & 0xFF;
    data[i * 4 + 3] = (values[i] >> 24) & 0xFF;
  }
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
  uint8_t data[8];
  const int size = f.read(data, sizeof(data));
  if (size != 4 && size != 8) return;
  const uint32_t storedPage = data[0] | (data[1] << 8) | (data[2] << 16) | (static_cast<uint32_t>(data[3]) << 24);
  const uint32_t storedCount =
      size == 8 ? data[4] | (data[5] << 8) | (data[6] << 16) | (static_cast<uint32_t>(data[7]) << 24) : 0;
  currentPage = storedPage;
  if (currentPage >= cbz->getPageCount() && cbz->getPageCount() > 0) {
    currentPage = cbz->getPageCount() - 1;
  }
  // Keep the file only when it matches the archive exactly; a 4-byte file from
  // an earlier build, a changed page count or a clamped page is rewritten on
  // the next save so the home screen reads the right percentage.
  const bool current = size == 8 && storedCount == cbz->getPageCount() && currentPage == storedPage;
  savedPage = current ? currentPage : UINT32_MAX;
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
