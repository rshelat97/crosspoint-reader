#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "util/Gray16Lut.h"

// Settings > General > "16-Level Lab": a grayscale test card drawn through the
// two-pass 16-level path, for tuning the fine-pass waveform from the SD card
// (docs/comics-gray16.md, "The lab screen"). Confirm reloads the LUT, Left /
// Right toggle a coarse-only reference, Back reloads the card's LUT (or clears
// it) so readers only use 16 levels when a file exists.
class Gray16LabActivity final : public Activity {
 public:
  Gray16LabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Gray16Lab", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Card geometry in logical pixels for the current orientation.
  struct Layout {
    int width;
    int height;
    int labelHeight;  // line height of the label font
    int bandBottom;   // 16 bands fill [0, bandBottom)
    int gridBottom;   // 4 x 4 grid fills [bandBottom, gridBottom); footer below
    int footerLine;   // line height of the footer's source line; the hint uses the label font
  };

  Layout makeLayout() const;
  void loadLut();
  void drawPlane(const Layout& l, uint8_t plane) const;
  void drawLabel(const Layout& l, int slotX, int y, int slotWidth, const char* text) const;
  void drawFooter(const Layout& l) const;
  void renderUnsupported(bool refused) const;
  const char* sourceLabel() const;

  uint8_t lut[gray16lut::LUT_SIZE] = {};
  gray16lut::Source source = gray16lut::Source::None;
  bool coarseOnly = false;  // pass 0 alone, as a reference for the fine steps
};
