// Simulator implementation of HalDisplay: same header, framebuffer in host
// memory, refreshes forwarded to the simulator display engine.
#include <HalDisplay.h>

#include <cstdlib>
#include <cstring>

#include "../sim/sim_display.h"

HalDisplay display;

namespace {
// 2-bit grayscale planes captured by the copy* calls until displayGrayBuffer.
uint8_t grayLsb[HalDisplay::BUFFER_SIZE];
uint8_t grayMsb[HalDisplay::BUFFER_SIZE];
bool frameBufferLent = false;
// Set by displayGrayscaleBase(Absolute): the next displayGrayBuffer() presents
// the planes as absolute pixels. Any B/W present or cleanup cancels it, like
// the SDK's cancelGrayscalePass().
bool absolutePending = false;

// CROSSPOINT_SIM_NO_GRAY16 forces the 4-level fallback path for testing. Read
// once; getenv may return null under Emscripten, which keeps 16-level on.
bool gray16Disabled() {
  static const bool disabled = getenv("CROSSPOINT_SIM_NO_GRAY16") != nullptr;
  return disabled;
}

void presentFrame(const uint8_t* fb, HalDisplay::RefreshMode mode) {
  absolutePending = false;
  simdisp::presentBW(fb, mode);
}
}  // namespace

HalDisplay::HalDisplay() : einkDisplay(0, 0, 0, 0, 0, 0) {}
HalDisplay::~HalDisplay() = default;

void HalDisplay::begin(bool) { clearScreen(); }

void HalDisplay::clearScreen(uint8_t color) const { memset(einkDisplay.frameBuffer(), color, BUFFER_SIZE); }

// Bit-blit of a 1-bpp image into the framebuffer at arbitrary x.
void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool) const {
  uint8_t* fb = einkDisplay.frameBuffer();
  const uint16_t srcStride = static_cast<uint16_t>((w + 7) / 8);
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t dy = y + row;
    if (dy >= DISPLAY_HEIGHT) break;
    for (uint16_t col = 0; col < w; col++) {
      const uint16_t dx = x + col;
      if (dx >= DISPLAY_WIDTH) break;
      const bool bit = (imageData[row * srcStride + col / 8] >> (7 - (col % 8))) & 1u;
      uint8_t& dst = fb[dy * DISPLAY_WIDTH_BYTES + dx / 8];
      const uint8_t mask = static_cast<uint8_t>(1u << (7 - (dx % 8)));
      if (bit)
        dst |= mask;
      else
        dst &= static_cast<uint8_t>(~mask);
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool) const {
  // Transparent variant: only black (0) source bits are drawn.
  uint8_t* fb = einkDisplay.frameBuffer();
  const uint16_t srcStride = static_cast<uint16_t>((w + 7) / 8);
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t dy = y + row;
    if (dy >= DISPLAY_HEIGHT) break;
    for (uint16_t col = 0; col < w; col++) {
      const uint16_t dx = x + col;
      if (dx >= DISPLAY_WIDTH) break;
      const bool bit = (imageData[row * srcStride + col / 8] >> (7 - (col % 8))) & 1u;
      if (bit) continue;
      fb[dy * DISPLAY_WIDTH_BYTES + dx / 8] &= static_cast<uint8_t>(~(1u << (7 - (dx % 8))));
    }
  }
}

void HalDisplay::displayBuffer(RefreshMode mode, bool) { presentFrame(einkDisplay.frameBuffer(), mode); }
void HalDisplay::displayBufferAsync(RefreshMode mode) { presentFrame(einkDisplay.frameBuffer(), mode); }
void HalDisplay::waitRefreshComplete() {}
bool HalDisplay::supportsAsyncRefresh() const { return false; }
void HalDisplay::refreshDisplay(RefreshMode mode, bool) { presentFrame(einkDisplay.frameBuffer(), mode); }

void HalDisplay::deepSleep() {}

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.frameBuffer(); }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) {
  if (frameBufferLent) return nullptr;
  frameBufferLent = true;
  if (sizeOut) *sizeOut = BUFFER_SIZE;
  return einkDisplay.frameBuffer();
}
void HalDisplay::returnFrameBufferStorage() {
  frameBufferLent = false;
  // Real hardware hands the buffer back white — reproduce so callers redraw.
  memset(einkDisplay.frameBuffer(), 0xFF, BUFFER_SIZE);
}

HalDisplay::Controller HalDisplay::getController() const { return BoardConfig::ACTIVE.displayController; }

// Overlay is what the X4's SSD1677 driver reports (B/W base, then light/dark
// masks composited over it) minus strip uploads and async base, which the
// simulator does not model. Absolute planes carry every pixel and present
// without a base. Direct is unsupported.
HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(GrayscaleMode mode) const {
  switch (mode) {
    case GrayscaleMode::Overlay:
      return {freeink::GrayscaleEncoding::OverlayMasks, freeink::GrayscaleBase::Separate, false, false, false};
    case GrayscaleMode::Absolute:
      return {freeink::GrayscaleEncoding::AbsolutePlanes, freeink::GrayscaleBase::Separate, false, false, false};
    default:
      return {};
  }
}

bool HalDisplay::displayGrayscaleBase(GrayscaleMode mode, RefreshMode fallback, bool turnOffScreen) {
  switch (mode) {
    case GrayscaleMode::Overlay:
      displayGrayscaleBase(fallback, turnOffScreen);
      return true;
    case GrayscaleMode::Absolute:
      // Nothing to show yet: the planes supply every pixel in displayGrayBuffer().
      absolutePending = true;
      return true;
    default:
      return false;
  }
}

void HalDisplay::setInverted(bool inverted) { simdisp::setInverted(inverted); }
bool HalDisplay::isInverted() const { return simdisp::isInverted(); }

void HalDisplay::preconditionGrayscale() {}
void HalDisplay::preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool) { presentFrame(einkDisplay.frameBuffer(), fallback); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  memcpy(grayLsb, lsbBuffer, BUFFER_SIZE);
  memcpy(grayMsb, msbBuffer, BUFFER_SIZE);
}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { memcpy(grayLsb, lsbBuffer, BUFFER_SIZE); }
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { memcpy(grayMsb, msbBuffer, BUFFER_SIZE); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) { absolutePending = false; }

void HalDisplay::displayGrayBuffer(bool) {
  if (absolutePending) {
    absolutePending = false;
    simdisp::presentGrayAbsolute(grayLsb, grayMsb);
    return;
  }
  simdisp::presentGray(grayLsb, grayMsb);
}

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
  uint8_t* plane = lsbPlane ? grayLsb : grayMsb;
  memcpy(&plane[static_cast<size_t>(yStart) * DISPLAY_WIDTH_BYTES], rows,
         static_cast<size_t>(numRows) * DISPLAY_WIDTH_BYTES);
}
bool HalDisplay::supportsStripGrayscale() const { return false; }

// 16-level pages: each pass feeds the staged planes to the two-pass model in
// sim_display; see docs/comics-gray16.md.
bool HalDisplay::supportsGray16() const { return !gray16Disabled(); }
bool HalDisplay::beginGray16() {
  if (gray16Disabled()) return false;
  simdisp::beginGray16();
  return true;
}
void HalDisplay::displayGray16Pass(uint8_t pass) { simdisp::presentGray16Pass(pass, grayLsb, grayMsb); }
void HalDisplay::endGray16(const uint8_t*) { simdisp::endGray16(); }

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
bool HalDisplay::toggleInverted() {
  simdisp::setInverted(!simdisp::isInverted());
  return simdisp::isInverted();
}
