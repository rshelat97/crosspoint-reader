#pragma once
// Simulator display engine: receives 1-bpp (and 2-bpp grayscale) framebuffer
// presents from the sim HalDisplay and exposes an RGBA image for the frontend
// (canvas blit in the browser, PNG/PPM dump natively).
#include <cstdint>

namespace simdisp {

constexpr int PANEL_W = 800;
constexpr int PANEL_H = 480;
constexpr int PANEL_STRIDE = PANEL_W / 8;
constexpr uint32_t BUFFER_SIZE = static_cast<uint32_t>(PANEL_STRIDE) * PANEL_H;

// Present a black/white frame. refreshMode: 0=FULL 1=HALF 2=FAST (display
// effect only — the frontend may flash on FULL).
void presentBW(const uint8_t* bw, int refreshMode);
// Present a 2-bit grayscale frame from LSB+MSB planes (0=black .. 3=white).
void presentGray(const uint8_t* lsb, const uint8_t* msb);

// Frontend side.
const uint8_t* rgba();           // PANEL_W*PANEL_H*4, updated on each present
uint32_t frameCounter();         // increments on each present
int lastRefreshMode();           // refresh mode of the latest present
bool dumpPpm(const char* path);  // native debugging: write current RGBA as PPM

}  // namespace simdisp
