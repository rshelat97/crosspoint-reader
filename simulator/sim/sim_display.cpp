#include "sim_display.h"

#include <cstdio>
#include <cstring>

namespace simdisp {
namespace {

uint8_t rgbaBuf[PANEL_W * PANEL_H * 4];
uint32_t frames = 0;
int lastMode = 0;

// Framebuffer convention (see GfxRenderer): 1 bit per pixel, MSB-first within
// each byte, bit set = white.
inline void writePixel(int x, int y, uint8_t lum) {
  uint8_t* p = &rgbaBuf[(static_cast<size_t>(y) * PANEL_W + x) * 4];
  p[0] = lum;
  p[1] = lum;
  p[2] = lum;
  p[3] = 0xFF;
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
  // The panel composites the two gray planes over the previously displayed
  // base frame: plane bits are ink coverage (set = darker), and value 0
  // leaves the base pixel untouched. Values 1..3 darken toward black.
  static constexpr uint8_t LEVELS[4] = {0xFF /*unused*/, 0xAA, 0x55, 0x00};
  for (int y = 0; y < PANEL_H; y++) {
    const size_t rowOff = static_cast<size_t>(y) * PANEL_STRIDE;
    for (int xb = 0; xb < PANEL_STRIDE; xb++) {
      const uint8_t lo = lsb[rowOff + xb];
      const uint8_t hi = msb[rowOff + xb];
      for (int bit = 0; bit < 8; bit++) {
        const uint8_t v = static_cast<uint8_t>((((hi >> (7 - bit)) & 1u) << 1) | ((lo >> (7 - bit)) & 1u));
        if (v == 0) continue;  // transparent: base frame pixel stays
        writePixel(xb * 8 + bit, y, LEVELS[v]);
      }
    }
  }
  frames++;
  lastMode = 1;
}

const uint8_t* rgba() { return rgbaBuf; }
uint32_t frameCounter() { return frames; }
int lastRefreshMode() { return lastMode; }

bool dumpPpm(const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fprintf(f, "P6\n%d %d\n255\n", PANEL_W, PANEL_H);
  for (size_t i = 0; i < sizeof(rgbaBuf); i += 4) fwrite(&rgbaBuf[i], 1, 3, f);
  fclose(f);
  return true;
}

}  // namespace simdisp
