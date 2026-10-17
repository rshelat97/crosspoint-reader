#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "sim_display.h"

namespace {

using Plane = std::array<uint8_t, simdisp::BUFFER_SIZE>;

// Sets one pixel of a plane in framebuffer layout (MSB first).
void setPlaneBit(Plane& plane, int x, int y, bool on) {
  uint8_t& byte = plane[static_cast<size_t>(y) * simdisp::PANEL_STRIDE + x / 8];
  const auto mask = static_cast<uint8_t>(0x80u >> (x % 8));
  if (on)
    byte |= mask;
  else
    byte &= static_cast<uint8_t>(~mask);
}

uint8_t red(int x, int y) { return simdisp::rgba()[(static_cast<size_t>(y) * simdisp::PANEL_W + x) * 4]; }

class SimDisplayTest : public ::testing::Test {
 protected:
  // The engine is process-global: start every test from a known white panel.
  void SetUp() override {
    simdisp::setInverted(false);
    white.fill(0xFF);
    black.fill(0x00);
    simdisp::presentBW(white.data(), 2);
  }
  void TearDown() override { simdisp::setInverted(false); }

  Plane white;
  Plane black;
};

TEST_F(SimDisplayTest, PresentBwMapsMsbFirstBitsToBlackAndWhite) {
  Plane bw = white;
  bw[0] = 0xA5;  // 1010 0101: x = 0..7
  const uint32_t before = simdisp::frameCounter();
  simdisp::presentBW(bw.data(), 0);

  const uint8_t expected[8] = {255, 0, 255, 0, 0, 255, 0, 255};
  for (int x = 0; x < 8; x++) {
    EXPECT_EQ(simdisp::luminanceAt(x, 0), expected[x]) << "x=" << x;
    EXPECT_EQ(red(x, 0), expected[x]) << "x=" << x;
  }
  EXPECT_EQ(simdisp::luminanceAt(8, 0), 255);
  EXPECT_EQ(simdisp::frameCounter(), before + 1);
  EXPECT_EQ(simdisp::lastRefreshMode(), 0);
}

TEST_F(SimDisplayTest, OverlayGrayLeavesUntouchedPixelsAndMapsLightAndDark) {
  // Base: x = 0 black, x = 1 white, both with (MSB, LSB) = 00 in the planes.
  Plane bw = white;
  setPlaneBit(bw, 0, 0, false);
  simdisp::presentBW(bw.data(), 2);

  Plane lsb = black;
  Plane msb = black;
  setPlaneBit(msb, 2, 0, true);  // (MSB, LSB) = 10: light gray
  setPlaneBit(msb, 3, 0, true);  // (MSB, LSB) = 11: dark gray
  setPlaneBit(lsb, 3, 0, true);
  const uint32_t before = simdisp::frameCounter();
  simdisp::presentGray(lsb.data(), msb.data());

  EXPECT_EQ(simdisp::luminanceAt(0, 0), 0);
  EXPECT_EQ(simdisp::luminanceAt(1, 0), 255);
  EXPECT_EQ(simdisp::luminanceAt(2, 0), 0xAA);
  EXPECT_EQ(simdisp::luminanceAt(3, 0), 0x55);
  EXPECT_EQ(simdisp::frameCounter(), before + 1);
  EXPECT_EQ(simdisp::lastRefreshMode(), 1);
}

TEST_F(SimDisplayTest, AbsoluteGrayReplacesEveryPixel) {
  simdisp::presentBW(black.data(), 2);  // every pixel must be overwritten, 00 included

  Plane lsb = black;
  Plane msb = black;
  for (int b = 0; b < 4; b++) {
    setPlaneBit(lsb, b, 0, b & 1);
    setPlaneBit(msb, b, 0, b & 2);
  }
  setPlaneBit(lsb, 4, 0, true);  // b = 3 over a black base
  setPlaneBit(msb, 4, 0, true);
  simdisp::presentGrayAbsolute(lsb.data(), msb.data());

  for (int b = 0; b < 4; b++) EXPECT_EQ(simdisp::luminanceAt(b, 0), b * 85) << "b=" << b;
  EXPECT_EQ(simdisp::luminanceAt(4, 0), 255);
  EXPECT_EQ(simdisp::luminanceAt(5, 0), 0);  // planes 00 = black, not "untouched"
  EXPECT_EQ(simdisp::lastRefreshMode(), 1);
}

TEST_F(SimDisplayTest, TwoGray16PassesReachBrightnessTimes17) {
  // Pixel (x, y) carries brightness g = (x + y) % 16 in planes 0..3.
  Plane planes[4];
  for (auto& p : planes) p.fill(0);
  for (int y = 0; y < simdisp::PANEL_H; y++) {
    for (int x = 0; x < simdisp::PANEL_W; x++) {
      const int g = (x + y) % 16;
      for (int k = 0; k < 4; k++) setPlaneBit(planes[k], x, y, (g >> k) & 1);
    }
  }

  const uint32_t before = simdisp::frameCounter();
  simdisp::beginGray16();
  simdisp::presentGray16Pass(0, planes[2].data(), planes[3].data());
  EXPECT_EQ(simdisp::lastRefreshMode(), 1);
  simdisp::presentGray16Pass(1, planes[0].data(), planes[1].data());
  simdisp::endGray16();
  EXPECT_EQ(simdisp::frameCounter(), before + 2);
  EXPECT_EQ(simdisp::lastRefreshMode(), 1);

  int mismatches = 0;
  for (int y = 0; y < simdisp::PANEL_H && mismatches < 8; y++) {
    for (int x = 0; x < simdisp::PANEL_W && mismatches < 8; x++) {
      const int g = (x + y) % 16;
      if (simdisp::luminanceAt(x, y) != g * 17) {
        mismatches++;
        ADD_FAILURE() << "(" << x << "," << y << ") g=" << g << " lum=" << int{simdisp::luminanceAt(x, y)};
      }
    }
  }
  EXPECT_EQ(mismatches, 0);
}

TEST_F(SimDisplayTest, CoarsePassAloneGivesFourLevelsFromWhite) {
  Plane lsb = black;
  Plane msb = black;
  for (int v = 0; v < 4; v++) {
    setPlaneBit(lsb, v, 0, v & 1);
    setPlaneBit(msb, v, 0, v & 2);
  }
  simdisp::presentGray16Pass(0, lsb.data(), msb.data());
  const uint8_t expected[4] = {51, 119, 187, 255};
  for (int v = 0; v < 4; v++) EXPECT_EQ(simdisp::luminanceAt(v, 0), expected[v]) << "v=" << v;
}

TEST_F(SimDisplayTest, InversionAppliesToRgbaOnly) {
  simdisp::setInverted(true);
  simdisp::presentBW(white.data(), 2);
  EXPECT_EQ(simdisp::luminanceAt(10, 10), 255);
  EXPECT_EQ(red(10, 10), 0);

  Plane lsb = black;
  Plane msb = black;
  setPlaneBit(lsb, 10, 10, true);  // b = 1: lum 85
  simdisp::presentGrayAbsolute(lsb.data(), msb.data());
  EXPECT_EQ(simdisp::luminanceAt(10, 10), 85);
  EXPECT_EQ(red(10, 10), 255 - 85);

  // Alpha stays opaque in both polarities.
  EXPECT_EQ(simdisp::rgba()[(static_cast<size_t>(10) * simdisp::PANEL_W + 10) * 4 + 3], 0xFF);
}

}  // namespace
