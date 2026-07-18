#pragma once
#include <I18n.h>

#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// In-reader typography panel, opened from the reader menu. Adjusts font size,
// line spacing, screen margin and paragraph spacing without leaving the book.
// Edits are written straight to SETTINGS in RAM; EpubReaderActivity persists
// and re-paginates on return only when a value actually changed.
class EpubReaderTextSettingsActivity final : public Activity {
 public:
  explicit EpubReaderTextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Item : uint8_t { FONT_SIZE, LINE_SPACING, SCREEN_MARGIN, PARAGRAPH_SPACING };

  struct MenuItem {
    Item item;
    StrId labelId;
  };

  static constexpr MenuItem MENU_ITEMS[] = {
      {Item::FONT_SIZE, StrId::STR_FONT_SIZE},
      {Item::LINE_SPACING, StrId::STR_LINE_SPACING},
      {Item::SCREEN_MARGIN, StrId::STR_SCREEN_MARGIN},
      {Item::PARAGRAPH_SPACING, StrId::STR_EXTRA_SPACING},
  };

  static constexpr StrId FONT_SIZE_LABELS[] = {StrId::STR_SMALL, StrId::STR_MEDIUM, StrId::STR_LARGE,
                                               StrId::STR_X_LARGE};
  static constexpr StrId LINE_SPACING_LABELS[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE};
  static constexpr StrId TOGGLE_LABELS[] = {StrId::STR_STATE_OFF, StrId::STR_STATE_ON};
  // Keep in sync with the screenMargin range registered in SettingsList.h ({5, 40, 5}).
  static constexpr uint8_t MARGIN_MIN = 5;
  static constexpr uint8_t MARGIN_STEP = 5;
  static constexpr const char* MARGIN_LABELS[] = {"5", "10", "15", "20", "25", "30", "35", "40"};

  static int fontSizeIndex();
  static int lineSpacingIndex();
  static int marginIndex();

  void openPopup(Item item);
  std::string valueLabel(int index) const;

  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
};
