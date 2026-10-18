#include "sim_display.h"

#include <cstdio>
#include <cstring>

namespace simdisp {
namespace {

// Per-pixel luminance held by the panel (0 black .. 255 white); the RGBA
// image is derived from it on every write.
uint8_t lum[PANEL_W * PANEL_H];
uint8_t rgbaBuf[PANEL_W * PANEL_H * 4];
uint32_t frames = 0;
int lastMode = 0;

bool inverted = false;  // panel-level dark mode (HalDisplay::setInverted)

inline void writePixel(int x, int y, uint8_t value) {
  const size_t idx = static_cast<size_t>(y) * PANEL_W + x;
  lum[idx] = value;
  if (inverted) value = static_cast<uint8_t>(0xFF - value);
  uint8_t* p = &rgbaBuf[idx * 4];
  p[0] = value;
  p[1] = value;
  p[2] = value;
  p[3] = 0xFF;
}

// Walks two planes in framebuffer layout and calls apply(x, y, v) with
// v = (msbBit << 1) | lsbBit for every pixel.
void forEachPlanePixel(const uint8_t* lsb, const uint8_t* msb, void (*apply)(int x, int y, uint8_t v)) {
  for (int y = 0; y < PANEL_H; y++) {
    const size_t rowOff = static_cast<size_t>(y) * PANEL_STRIDE;
    for (int xb = 0; xb < PANEL_STRIDE; xb++) {
      const uint8_t lo = lsb[rowOff + xb];
      const uint8_t hi = msb[rowOff + xb];
      for (int bit = 0; bit < 8; bit++) {
        const uint8_t v = static_cast<uint8_t>((((hi >> (7 - bit)) & 1u) << 1) | ((lo >> (7 - bit)) & 1u));
        apply(xb * 8 + bit, y, v);
      }
    }
  }
}

void applyOverlay(int x, int y, uint8_t v) {
  // The panel composites the two gray planes over the previously displayed
  // base frame (Overlay grayscale). Mask encoding, per the SDK's
  // GrayscaleCapabilities and GlyphBitmap: (MSB,LSB) 00 = untouched base
  // pixel (black and white both live in the B/W base), 10 = light gray,
  // 11 = dark gray. 01 is never produced; it is shown as light gray.
  static constexpr uint8_t LEVELS[4] = {0xFF /*transparent*/, 0xAA, 0xAA, 0x55};
  if (v == 0) return;  // transparent: base frame pixel stays
  writePixel(x, y, LEVELS[v]);
}

void applyAbsolute(int x, int y, uint8_t v) { writePixel(x, y, static_cast<uint8_t>(v * 85)); }

// Coarse pass: starts from white, each missing step darkens by 68.
void applyCoarse(int x, int y, uint8_t v) { writePixel(x, y, static_cast<uint8_t>(255 - (3 - v) * 68)); }

// Fine pass: each missing step darkens the coarse result by 17.
void applyFine(int x, int y, uint8_t v) {
  const int current = lum[static_cast<size_t>(y) * PANEL_W + x];
  const int next = current - (3 - v) * 17;
  writePixel(x, y, static_cast<uint8_t>(next < 0 ? 0 : next));
}

}  // namespace

void presentBW(const uint8_t* bw, int refreshMode) {
  for (int y = 0; y < PANEL_H; y++) {
    const uint8_t* row = &bw[static_cast<size_t>(y) * PANEL_STRIDE];
    for (int xb = 0; xb < PANEL_STRIDE; xb++) {
      const uint8_t byte = row[xb];
      for (int bit = 0; bit < 8; bit++) {
        const bool white = (byte >> (7 - bit)) & 1u;
        writePixel(xb * 8 + bit, y, white ? 0xFF : 0x00);
      }
    }
  }
  frames++;
  lastMode = refreshMode;
}

void presentGray(const uint8_t* lsb, const uint8_t* msb) {
  forEachPlanePixel(lsb, msb, applyOverlay);
  frames++;
  lastMode = 1;
}

void presentGrayAbsolute(const uint8_t* lsb, const uint8_t* msb) {
  forEachPlanePixel(lsb, msb, applyAbsolute);
  frames++;
  lastMode = 1;
}

void beginGray16() {}

void presentGray16Pass(int pass, const uint8_t* lsb, const uint8_t* msb) {
  forEachPlanePixel(lsb, msb, pass == 0 ? applyCoarse : applyFine);
  frames++;
  lastMode = 1;  // non-flashing update, like an overlay gray pass
}

void endGray16() {}

void setInverted(bool on) { inverted = on; }
bool isInverted() { return inverted; }

const uint8_t* rgba() { return rgbaBuf; }
uint32_t frameCounter() { return frames; }
int lastRefreshMode() { return lastMode; }

uint8_t luminanceAt(int x, int y) {
  if (x < 0 || y < 0 || x >= PANEL_W || y >= PANEL_H) return 0;
  return lum[static_cast<size_t>(y) * PANEL_W + x];
}

bool dumpPpm(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", PANEL_W, PANEL_H);
  for (size_t i = 0; i < sizeof(rgbaBuf); i += 4) fwrite(&rgbaBuf[i], 1, 3, f);
  fclose(f);
  return true;
}

}  // namespace simdisp
