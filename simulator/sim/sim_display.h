#pragma once
// Simulator display engine: receives 1-bpp (and 2-bpp grayscale) framebuffer
// presents from the sim HalDisplay and exposes an RGBA image for the frontend
// (canvas blit in the browser, PNG/PPM dump natively).
//
// Planes use the framebuffer layout: 8 pixels per byte, MSB first (bit 7 is
// the leftmost column), rows of PANEL_STRIDE bytes, bit set = 1.
#include <cstdint>

namespace simdisp {

constexpr int PANEL_W = 800;
constexpr int PANEL_H = 480;
constexpr int PANEL_STRIDE = PANEL_W / 8;
constexpr uint32_t BUFFER_SIZE = static_cast<uint32_t>(PANEL_STRIDE) * PANEL_H;

// Present a black/white frame. refreshMode: 0=FULL 1=HALF 2=FAST (display
// effect only — the frontend may flash on FULL).
void presentBW(const uint8_t* bw, int refreshMode);
// Overlay grayscale: composite LSB+MSB masks over the displayed frame
// ((MSB, LSB) 00 = untouched, 10 = light gray, 11 = dark gray).
void presentGray(const uint8_t* lsb, const uint8_t* msb);
// Absolute grayscale: every pixel becomes (msbBit << 1) | lsbBit, 0 = black
// .. 3 = white.
void presentGrayAbsolute(const uint8_t* lsb, const uint8_t* msb);

// 16-level grayscale as two absolute passes (docs/comics-gray16.md). With
// v = (msbBit << 1) | lsbBit, pass 0 (coarse) sets lum = 255 - (3 - v) * 68
// and pass 1 (fine) subtracts (3 - v) * 17, so brightness g = 0..15 ends at
// g * 17. begin/end only bracket the passes.
void beginGray16();
void presentGray16Pass(int pass, const uint8_t* lsb, const uint8_t* msb);
void endGray16();

// Panel-level inversion (dark mode): applies to subsequent presents.
void setInverted(bool on);
bool isInverted();

// Frontend side.
const uint8_t* rgba();           // PANEL_W*PANEL_H*4, updated on each present
uint32_t frameCounter();         // increments on each present
int lastRefreshMode();           // refresh mode of the latest present
bool dumpPpm(const char* path);  // native debugging: write current RGBA as PPM

// Luminance the panel holds at (x, y), before inversion (tests).
uint8_t luminanceAt(int x, int y);

}  // namespace simdisp
