#include <HalDisplay.h>
#include <HalGPIO.h>

#include <cstring>

// Global HalDisplay instance
HalDisplay display;

namespace {
// Coarse-pass uploads. The factory 4-level LUT selects black at 11 and white
// at 00, the complement of the public plane encoding. Absolute mode would have
// the driver complement, but the fine pass needs the facade's Overlay mode
// (see supportsGray16), so the planes are streamed as complemented strips from
// a small stack chunk instead (240 two-row strips per plane on the X4).
constexpr size_t COARSE_CHUNK_BYTES = 240;

void writeComplementedRows(EInkDisplay& eink, EInkDisplay::GrayPlane plane, const uint8_t* rows, uint16_t yStart,
                           uint16_t numRows) {
  const uint16_t widthBytes = eink.getDisplayWidthBytes();
  if (!rows || widthBytes == 0 || widthBytes > COARSE_CHUNK_BYTES) return;
  uint8_t chunk[COARSE_CHUNK_BYTES];
  const uint16_t rowsPerStrip = static_cast<uint16_t>(sizeof(chunk) / widthBytes);
  for (uint16_t y = 0; y < numRows; y += rowsPerStrip) {
    const uint16_t count = static_cast<uint16_t>(numRows - y < rowsPerStrip ? numRows - y : rowsPerStrip);
    const size_t bytes = static_cast<size_t>(count) * widthBytes;
    const uint8_t* src = rows + static_cast<size_t>(y) * widthBytes;
    for (size_t i = 0; i < bytes; i++) chunk[i] = static_cast<uint8_t>(~src[i]);
    eink.writeGrayscalePlaneStrip(plane, chunk, static_cast<uint16_t>(yStart + y), count);
  }
}
}  // namespace

#define SD_SPI_MISO 7

HalDisplay::HalDisplay() : einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

// absoluteGrayPending is true from a successful displayGrayscaleBase(Absolute)
// until the SDK is clean again. The facade ends an Absolute pass through
// cancelGrayscalePass(), which sets its private _grayPassFailed flag, and
// FreeInkDisplay::displayGrayBuffer() then returns early until a B/W refresh or
// a grayscale base clears it: a 16-level sequence started in that state would
// upload its planes but never run a waveform. The driver's own complement flag
// (_absoluteInput) is cleared only by a pass that ran or by an Overlay base, so
// after an aborted pass (a failed image decode) a B/W refresh is not enough:
// the coarse planes would be complemented twice.
void HalDisplay::noteBwRefresh() {
  if (absoluteGrayRan) absoluteGrayPending = false;
  gray16Stage = Gray16Stage::Idle;
}

void HalDisplay::noteOverlayBase() {
  absoluteGrayPending = false;
  absoluteGrayRan = false;
  gray16Stage = Gray16Stage::Idle;
}

HalDisplay::~HalDisplay() {}

HalDisplay::Controller HalDisplay::getController() const { return BoardConfig::ACTIVE.displayController; }

void HalDisplay::begin(bool seamless) {
  // Set X3-specific panel mode before initializing.
  if (gpio.deviceIsX3()) {
    einkDisplay.setDisplayX3();
  }

  einkDisplay.begin();

  if (seamless) {
    // Defuse the SDK's X3 _x3InitialFullSyncsRemaining counter (no-op on X4)
    // so the first paint isn't promoted to FULL (~770ms). Skips the wakeup-
    // gated requestResync() below for the same reason.
    einkDisplay.skipInitialResync();
    return;
  }
  // Request resync after specific wakeup events to ensure clean display state.
  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton || wakeupReason == HalGPIO::WakeupReason::AfterFlash ||
      wakeupReason == HalGPIO::WakeupReason::Other) {
    einkDisplay.requestResync();
  }
}

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  einkDisplay.drawImageTransparent(imageData, x, y, w, h, fromProgmem);
}

EInkDisplay::RefreshMode convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  noteBwRefresh();
  einkDisplay.displayBuffer(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::displayBufferAsync(HalDisplay::RefreshMode mode) {
  if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  noteBwRefresh();
  einkDisplay.displayBufferAsyncNoShadow(convertRefreshMode(mode));
}

void HalDisplay::waitRefreshComplete() { einkDisplay.waitRefreshComplete(); }

bool HalDisplay::supportsAsyncRefresh() const { return einkDisplay.supportsAsyncRefresh(); }

HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(GrayscaleMode mode) const {
  return einkDisplay.grayscaleCapabilities(mode);
}

bool HalDisplay::supportsAsyncGrayscaleBase() const { return grayscaleCapabilities().asyncBase; }

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  noteBwRefresh();
  einkDisplay.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::setInverted(bool inverted) { einkDisplay.setInverted(inverted); }

bool HalDisplay::toggleInverted() { return einkDisplay.toggleInverted(); }

bool HalDisplay::isInverted() const { return einkDisplay.isInverted(); }

void HalDisplay::deepSleep() { einkDisplay.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) { return einkDisplay.lendBuildStorage(sizeOut); }

void HalDisplay::returnFrameBufferStorage() { einkDisplay.returnBuildStorage(); }

bool HalDisplay::displayGrayscaleBase(GrayscaleMode mode, RefreshMode fallback, bool turnOffScreen) {
  if (gpio.deviceIsX3() && fallback == HALF_REFRESH) einkDisplay.requestResync();
  gray16Stage = Gray16Stage::Idle;
  const bool ok =
      einkDisplay.displayGrayscaleBase(mode, static_cast<EInkDisplay::RefreshMode>(fallback), turnOffScreen);
  if (ok) {
    absoluteGrayPending = mode == GrayscaleMode::Absolute;
    absoluteGrayRan = false;
  }
  return ok;
}

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  if (gray16Stage == Gray16Stage::Coarse) {
    writeComplementedRows(einkDisplay, EInkDisplay::GRAY_PLANE_LSB, lsbBuffer, 0, einkDisplay.getDisplayHeight());
    writeComplementedRows(einkDisplay, EInkDisplay::GRAY_PLANE_MSB, msbBuffer, 0, einkDisplay.getDisplayHeight());
    return;
  }
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool turnOffScreen) {
  // X3: a HALF fallback means the caller wants a clean base (e.g. the sleep
  // cover, a full-screen swap from arbitrary prior content). Without this, the
  // X3 grayscale base takes its gentle differential happy path and the prior
  // home/reader frame ghosts through the soft aa_pre_bw_mid waveform. Forcing a
  // resync makes displayGrayscaleBase clear first, matching displayBuffer(HALF).
  // The reader's FAST path is deliberately left on the differential path so
  // per-page grayscale stays cheap.
  if (gpio.deviceIsX3() && fallback == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  noteOverlayBase();
  einkDisplay.displayGrayscaleBase(convertRefreshMode(fallback), turnOffScreen);
}

void HalDisplay::preconditionGrayscale() { einkDisplay.preconditionGrayscale(); }

void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  einkDisplay.preconditionGrayscale(x, y, w, h);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (gray16Stage == Gray16Stage::Coarse) {
    writeComplementedRows(einkDisplay, EInkDisplay::GRAY_PLANE_LSB, lsbBuffer, 0, einkDisplay.getDisplayHeight());
    return;
  }
  einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (gray16Stage == Gray16Stage::Coarse) {
    writeComplementedRows(einkDisplay, EInkDisplay::GRAY_PLANE_MSB, msbBuffer, 0, einkDisplay.getDisplayHeight());
    return;
  }
  einkDisplay.copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  gray16Stage = Gray16Stage::Idle;
  einkDisplay.cleanupGrayscaleBuffers(bwBuffer);
}

void HalDisplay::displayGrayBuffer(bool turnOffScreen) {
  // An Absolute pass that reaches the driver also clears its complement flag.
  if (absoluteGrayPending) absoluteGrayRan = true;
  einkDisplay.displayGrayBuffer(turnOffScreen);
}

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
  const auto plane = lsbPlane ? EInkDisplay::GRAY_PLANE_LSB : EInkDisplay::GRAY_PLANE_MSB;
  if (gray16Stage == Gray16Stage::Coarse) {
    writeComplementedRows(einkDisplay, plane, rows, yStart, numRows);
    return;
  }
  einkDisplay.writeGrayscalePlaneStrip(plane, rows, yStart, numRows);
}

bool HalDisplay::supportsStripGrayscale() const { return grayscaleCapabilities().stripUploads; }

bool HalDisplay::combinesGrayscaleBase() const { return grayscaleCapabilities().base == GrayscaleBase::Combined; }

// 16-level pages: two factory-mode refreshes staged in the facade's Overlay
// mode (HalDisplay.h has the protocol). Absolute mode is never entered:
// FreeInkDisplay::displayGrayBuffer() rejects a custom LUT there, and ending an
// Absolute pass leaves the facade refusing further grayscale passes until a
// B/W refresh (absoluteGrayPending).
bool HalDisplay::supportsGray16() const {
  // The factory pass and the fine LUT are SSD1677 waveforms: the X3's UC8253
  // and the UC8279 advertise the same capabilities but ignore custom LUTs.
  // Overlay staging carries both passes, strip uploads carry the coarse
  // complement (whose chunk must hold a row), and the Absolute capability
  // marks a verified factory pass. Inverted output reports no capabilities.
  return gray16FineLutSet && getController() == Controller::SSD1677 &&
         einkDisplay.getDisplayWidthBytes() <= COARSE_CHUNK_BYTES &&
         grayscaleCapabilities(GrayscaleMode::Absolute).supported() &&
         grayscaleCapabilities(GrayscaleMode::Overlay).supported() && supportsStripGrayscale();
}

bool HalDisplay::beginGray16() {
  if (!supportsGray16() || absoluteGrayPending) return false;
  gray16Stage = Gray16Stage::Coarse;
  return true;
}

void HalDisplay::displayGray16Pass(uint8_t pass, bool turnOffScreen) {
  switch (pass) {
    case 0:
      // The board's own factory 4-level pass (lut == nullptr) on the complemented planes.
      if (gray16Stage != Gray16Stage::Coarse) break;  // no beginGray16(): planes are raw
      einkDisplay.displayGrayBuffer(turnOffScreen, nullptr, /*factoryMode=*/true);
      gray16Stage = Gray16Stage::Fine;
      break;
    case 1:
      // The driver reads the LUT with pgm_read_byte, a plain byte load on ESP32, so RAM is fine.
      if (gray16Stage != Gray16Stage::Fine || !gray16FineLutSet) break;  // no coarse pass before it
      einkDisplay.displayGrayBuffer(turnOffScreen, gray16FineLut, /*factoryMode=*/true);
      break;
    default:
      break;
  }
}

void HalDisplay::endGray16(const uint8_t* bwBaseline) {
  gray16Stage = Gray16Stage::Idle;
  if (bwBaseline) cleanupGrayscaleBuffers(bwBaseline);
}

void HalDisplay::setGray16FineLut(const uint8_t* lut112) {
  gray16FineLutSet = lut112 != nullptr;
  if (lut112) memcpy(gray16FineLut, lut112, GRAY16_LUT_SIZE);
}

uint16_t HalDisplay::getDisplayWidth() const { return einkDisplay.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return einkDisplay.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return einkDisplay.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return einkDisplay.getBufferSize(); }
