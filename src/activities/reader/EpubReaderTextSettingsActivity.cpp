#include "EpubReaderTextSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

EpubReaderTextSettingsActivity::EpubReaderTextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("EpubReaderTextSettings", renderer, mappedInput) {}

void EpubReaderTextSettingsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

int EpubReaderTextSettingsActivity::fontSizeIndex() {
  return std::min(static_cast<int>(SETTINGS.fontSize), static_cast<int>(std::size(FONT_SIZE_LABELS)) - 1);
}

int EpubReaderTextSettingsActivity::lineSpacingIndex() {
  return std::min(static_cast<int>(SETTINGS.lineSpacing), static_cast<int>(std::size(LINE_SPACING_LABELS)) - 1);
}

int EpubReaderTextSettingsActivity::marginIndex() {
  const int idx = (static_cast<int>(SETTINGS.screenMargin) - MARGIN_MIN) / MARGIN_STEP;
  return std::clamp(idx, 0, static_cast<int>(std::size(MARGIN_LABELS)) - 1);
}

void EpubReaderTextSettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(std::size(MENU_ITEMS)));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(std::size(MENU_ITEMS)));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openPopup(MENU_ITEMS[selectedIndex].item);
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void EpubReaderTextSettingsActivity::openPopup(const Item item) {
  switch (item) {
    case Item::FONT_SIZE:
      optionPopup.show(StrId::STR_FONT_SIZE, FONT_SIZE_LABELS, static_cast<int>(std::size(FONT_SIZE_LABELS)),
                       fontSizeIndex(), [this](int idx) {
                         SETTINGS.fontSize = static_cast<uint8_t>(idx);
                         requestUpdate();
                       });
      break;
    case Item::LINE_SPACING:
      optionPopup.show(StrId::STR_LINE_SPACING, LINE_SPACING_LABELS, static_cast<int>(std::size(LINE_SPACING_LABELS)),
                       lineSpacingIndex(), [this](int idx) {
                         SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                         requestUpdate();
                       });
      break;
    case Item::SCREEN_MARGIN:
      optionPopup.show(I18N.get(StrId::STR_SCREEN_MARGIN), MARGIN_LABELS, static_cast<int>(std::size(MARGIN_LABELS)),
                       marginIndex(), [this](int idx) {
                         SETTINGS.screenMargin = static_cast<uint8_t>(MARGIN_MIN + idx * MARGIN_STEP);
                         requestUpdate();
                       });
      break;
    case Item::PARAGRAPH_SPACING:
      optionPopup.show(StrId::STR_EXTRA_SPACING, TOGGLE_LABELS, static_cast<int>(std::size(TOGGLE_LABELS)),
                       SETTINGS.extraParagraphSpacing ? 1 : 0, [this](int idx) {
                         SETTINGS.extraParagraphSpacing = static_cast<uint8_t>(idx);
                         requestUpdate();
                       });
      break;
  }
}

std::string EpubReaderTextSettingsActivity::valueLabel(const int index) const {
  switch (MENU_ITEMS[index].item) {
    case Item::FONT_SIZE:
      return I18N.get(FONT_SIZE_LABELS[fontSizeIndex()]);
    case Item::LINE_SPACING:
      return I18N.get(LINE_SPACING_LABELS[lineSpacingIndex()]);
    case Item::SCREEN_MARGIN:
      return MARGIN_LABELS[marginIndex()];
    case Item::PARAGRAPH_SPACING:
      return I18N.get(SETTINGS.extraParagraphSpacing ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
  }
  return "";
}

void EpubReaderTextSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_TEXT_SETTINGS));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, static_cast<int>(std::size(MENU_ITEMS)),
      selectedIndex, [](int index) { return std::string(I18N.get(MENU_ITEMS[index].labelId)); }, nullptr, nullptr,
      [this](int index) { return valueLabel(index); }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
