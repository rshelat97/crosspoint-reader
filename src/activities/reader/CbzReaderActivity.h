#pragma once

#include <Cbz.h>

#include <cstdint>
#include <memory>
#include <string>

#include "ReaderActivity.h"

// Comic (CBZ) reader: streams 16-level page planes from the Gray16 cache and
// shows them as two absolute passes, or as the 4-level overlay on panels
// without 16-level waveforms (docs/comics-gray16.md). Page turns, progress
// and the status bar follow XtcReaderActivity.
class CbzReaderActivity final : public ReaderActivity {
  std::unique_ptr<Cbz> cbz;
  uint32_t currentPage = 0;
  uint32_t savedPage = UINT32_MAX;  // page held by progress.bin
  // Allocated once in onEnter(): one plane tile for the Gray16 reader and, on
  // panels without 16-level support, a panel-sized plane for the overlay masks.
  std::unique_ptr<uint8_t[]> tileScratch;
  size_t tileScratchLen = 0;
  std::unique_ptr<uint8_t[]> planeScratch;

  void renderPage();
  bool renderGray16(uint8_t* framebuffer);
  bool renderGray4(uint8_t* framebuffer);
  bool loadPlaneWithStatusBar(uint8_t plane, uint8_t* framebuffer) const;
  void renderErrorPage(const char* message) const;
  bool statusBarLayout(int* clearY, int* clearHeight, int* paddingBottom) const;
  void renderStatusBarOverlay() const;
  void clearStatusBarMask() const;
  void saveProgress();
  void loadProgress();

  bool loadBook() override;
  std::string getBookTitle() const override { return cbz ? cbz->getTitle() : ""; }
  std::string getBookThumbBmpPath() const override { return cbz ? cbz->getThumbBmpPath() : ""; }
  void renderBook() override;

 public:
  explicit CbzReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("CbzReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~CbzReaderActivity() override = default;

  void onEnter() override;
  void onExit() override;
  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  ScreenshotInfo getScreenshotInfo() const override;
};
