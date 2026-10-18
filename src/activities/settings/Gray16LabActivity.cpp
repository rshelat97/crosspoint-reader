#include "Gray16LabActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* TAG = "G16LAB";
constexpr int LEVELS = 16;
constexpr int GRID = 4;  // coarse levels (rows) x fine classes (columns)
constexpr int PAD = 4;

// Left edge of slot `index` when `extent` pixels are split into `count` slots.
int slotEdge(const int index, const int count, const int extent) { return index * extent / count; }

const char* baseName(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

// Log helpers take their inputs as parameters so the values stay used in
// builds that compile the log macros out.
void logHex(const char* name, const uint8_t* bytes, const size_t len) {
  char hex[2 * 50 + 1] = "";
  for (size_t i = 0; i < len && 2 * i + 2 < sizeof(hex); i++) snprintf(&hex[2 * i], 3, "%02X", bytes[i]);
  LOG_INF(TAG, "  %s %s", name, hex);
}

void logRender(const char* view, const uint32_t passMs[2]) {
  LOG_INF(TAG, "%s: pass 0 %lu ms, pass 1 %lu ms, heap %u free, %u max block", view,
          static_cast<unsigned long>(passMs[0]), static_cast<unsigned long>(passMs[1]),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
}
}  // namespace

void Gray16LabActivity::onEnter() {
  Activity::onEnter();
  loadLut();
  requestUpdate();
}

// Back to the boot state: readers use 16 levels only when the card has a LUT.
void Gray16LabActivity::onExit() {
  renderer.setGray16FineLut(gray16lut::loadFromStorage(lut) != gray16lut::Source::None ? lut : nullptr);
  Activity::onExit();
}

void Gray16LabActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    {
      RenderLock lock(*this);
      loadLut();
    }
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    {
      RenderLock lock(*this);
      coarseOnly = !coarseOnly;
    }
    requestUpdate();
  }
}

// fine.lut / fine.cfg from the card, else the built-in default so the lab works
// on a fresh card. The LUT goes to the display at once.
void Gray16LabActivity::loadLut() {
  source = gray16lut::loadFromStorage(lut);
  if (source == gray16lut::Source::None) {
    const gray16lut::FineConfig cfg{};
    gray16lut::buildLut(cfg, lut);
    LOG_INF(TAG, "Fine LUT: built-in default (darken %d %d %d, fr %d, vgh %02X vsh1 %02X vsh2 %02X vsl %02X vcom %02X)",
            cfg.darken[0], cfg.darken[1], cfg.darken[2], cfg.frameRate, cfg.voltages[0], cfg.voltages[1],
            cfg.voltages[2], cfg.voltages[3], cfg.voltages[4]);
  } else {
    LOG_INF(TAG, "Fine LUT: %s",
            source == gray16lut::Source::RawFile ? gray16lut::RAW_LUT_PATH : gray16lut::CONFIG_PATH);
  }
  renderer.setGray16FineLut(lut);
  // The bytes as the controller receives them (docs/comics-gray16.md, "Fine LUT format").
  logHex("VS rows", lut, 50);
  logHex("TP groups", lut + 50, 50);
  logHex("FR + voltages", lut + 100, 12);
}

void Gray16LabActivity::render(RenderLock&&) {
  const bool supported = renderer.supportsGray16();
  if (!supported || !renderer.beginGray16()) {
    // The HAL refuses a sequence until a B/W refresh follows an Absolute-mode
    // pass; the plain screen below is that refresh, so Confirm retries.
    if (supported) LOG_INF(TAG, "16-level sequence refused; press Confirm to retry");
    renderUnsupported();
    return;
  }
  const Layout layout = makeLayout();
  const uint8_t passCount = coarseOnly ? 1 : 2;
  uint32_t passMs[2] = {0, 0};
  for (uint8_t pass = 0; pass < passCount; pass++) {
    const uint8_t low = pass == 0 ? 2 : 0;
    drawPlane(layout, low);
    renderer.copyGrayscaleLsbBuffers();
    drawPlane(layout, low + 1);
    renderer.copyGrayscaleMsbBuffers();
    const uint32_t start = millis();
    renderer.displayGray16Pass(pass);
    passMs[pass] = millis() - start;
  }
  // Plane 3 is the B/W approximation endGray16() keeps as the next baseline.
  drawPlane(layout, 3);
  renderer.endGray16();
  logRender(coarseOnly ? "coarse pass only" : "full card", passMs);
}

Gray16LabActivity::Layout Gray16LabActivity::makeLayout() const {
  Layout l{};
  l.width = renderer.getScreenWidth();
  l.height = renderer.getScreenHeight();
  l.labelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  l.footerLine = renderer.getLineHeight(UI_10_FONT_ID);
  l.gridBottom = l.height - l.footerLine - l.labelHeight - 2 * PAD;
  l.bandBottom = l.gridBottom * 3 / 5;
  return l;
}

// One plane of the card, straight into the framebuffer: a region at level g is
// white where bit `plane` of g is set, black otherwise. Labels and footer are
// identical in every plane, so they stay pure black and white.
void Gray16LabActivity::drawPlane(const Layout& l, const uint8_t plane) const {
  renderer.clearScreen();
  for (int g = 0; g < LEVELS; g++) {
    if ((g >> plane) & 1) continue;
    const int x0 = slotEdge(g, LEVELS, l.width);
    renderer.fillRect(x0, 0, slotEdge(g + 1, LEVELS, l.width) - x0, l.bandBottom, true);
  }
  const int gridHeight = l.gridBottom - l.bandBottom;
  for (int row = 0; row < GRID; row++) {
    const int y0 = l.bandBottom + slotEdge(row, GRID, gridHeight);
    const int y1 = l.bandBottom + slotEdge(row + 1, GRID, gridHeight);
    for (int col = 0; col < GRID; col++) {
      const int g = (row << 2) | col;
      if ((g >> plane) & 1) continue;
      const int x0 = slotEdge(col, GRID, l.width);
      renderer.fillRect(x0, y0, slotEdge(col + 1, GRID, l.width) - x0, y1 - y0, true);
    }
  }
  char text[4];
  for (int g = 0; g < LEVELS; g++) {
    snprintf(text, sizeof(text), "%d", g);
    const int bandX = slotEdge(g, LEVELS, l.width);
    drawLabel(l, bandX, PAD, slotEdge(g + 1, LEVELS, l.width) - bandX, text);
    const int col = g & 3;
    const int cellX = slotEdge(col, GRID, l.width);
    const int cellY = l.bandBottom + slotEdge(g >> 2, GRID, gridHeight) + PAD;
    drawLabel(l, cellX, cellY, slotEdge(col + 1, GRID, l.width) - cellX, text);
  }
  drawFooter(l);
}

// Level number in a white box with black text, centred at the top of a slot.
void Gray16LabActivity::drawLabel(const Layout& l, const int slotX, const int y, const int slotWidth,
                                  const char* text) const {
  const int boxWidth = renderer.getTextWidth(SMALL_FONT_ID, text) + 2 * PAD;
  const int x = slotX + (slotWidth - boxWidth) / 2;
  renderer.fillRect(x, y, boxWidth, l.labelHeight + 2, false);
  renderer.drawText(SMALL_FONT_ID, x + PAD, y + 1, text, true);
}

void Gray16LabActivity::drawFooter(const Layout& l) const {
  char line[128];
  snprintf(line, sizeof(line), "%s %s%s%s", tr(STR_GRAY16_LUT_SOURCE), sourceLabel(), coarseOnly ? "  |  " : "",
           coarseOnly ? tr(STR_GRAY16_COARSE_ONLY) : "");
  const int y = l.gridBottom + PAD;
  renderer.drawText(UI_10_FONT_ID, PAD, y, line, true);
  renderer.drawText(SMALL_FONT_ID, PAD, y + l.footerLine, tr(STR_GRAY16_LAB_HINT), true);
}

// Other panels and the simulator with CROSSPOINT_SIM_NO_GRAY16: plain B/W.
void Gray16LabActivity::renderUnsupported() const {
  LOG_INF(TAG, "16-level path unsupported on this panel");
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_GRAY16_LAB));
  renderer.drawCenteredText(UI_10_FONT_ID, height / 2 - 20, tr(STR_GRAY16_UNSUPPORTED), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, height / 2 + 10, tr(STR_GRAY16_LAB_HINT), true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

const char* Gray16LabActivity::sourceLabel() const {
  switch (source) {
    case gray16lut::Source::RawFile:
      return baseName(gray16lut::RAW_LUT_PATH);
    case gray16lut::Source::Config:
      return baseName(gray16lut::CONFIG_PATH);
    case gray16lut::Source::None:
      break;
  }
  return tr(STR_GRAY16_LUT_DEFAULT);
}
