// Simulator implementation of HalDisplay: same header, framebuffer in host
// memory, refreshes forwarded to the simulator display engine.
#include <HalDisplay.h>

#include <cstring>

#include "../sim/sim_display.h"

HalDisplay display;

namespace {
// 2-bit grayscale planes captured by the copy* calls until displayGrayBuffer.
uint8_t grayLsb[HalDisplay::BUFFER_SIZE];
uint8_t grayMsb[HalDisplay::BUFFER_SIZE];
bool frameBufferLent = false;
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

void HalDisplay::displayBuffer(RefreshMode mode, bool) { simdisp::presentBW(einkDisplay.frameBuffer(), mode); }
void HalDisplay::displayBufferAsync(RefreshMode mode) { simdisp::presentBW(einkDisplay.frameBuffer(), mode); }
void HalDisplay::waitRefreshComplete() {}
bool HalDisplay::supportsAsyncRefresh() const { return false; }
void HalDisplay::refreshDisplay(RefreshMode mode, bool) { simdisp::presentBW(einkDisplay.frameBuffer(), mode); }

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

void HalDisplay::preconditionGrayscale() {}
void HalDisplay::preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool) {
  simdisp::presentBW(einkDisplay.frameBuffer(), fallback);
}

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  memcpy(grayLsb, lsbBuffer, BUFFER_SIZE);
  memcpy(grayMsb, msbBuffer, BUFFER_SIZE);
}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { memcpy(grayLsb, lsbBuffer, BUFFER_SIZE); }
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { memcpy(grayMsb, msbBuffer, BUFFER_SIZE); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}

void HalDisplay::displayGrayBuffer(bool) { simdisp::presentGray(grayLsb, grayMsb); }

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
  uint8_t* plane = lsbPlane ? grayLsb : grayMsb;
  memcpy(&plane[static_cast<size_t>(yStart) * DISPLAY_WIDTH_BYTES], rows,
         static_cast<size_t>(numRows) * DISPLAY_WIDTH_BYTES);
}
bool HalDisplay::supportsStripGrayscale() const { return false; }

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
