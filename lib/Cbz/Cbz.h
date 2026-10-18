#pragma once

#include <Gray16.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Entry filtering and ordering for CBZ archives (docs/comics-gray16.md).
namespace cbz {
// A page image entry: .jpg/.jpeg/.png (case-insensitive), not a directory
// entry, not under __MACOSX/, and a basename that does not start with '.'.
bool isPageEntry(std::string_view name);
// Case-insensitive natural order ("2.jpg" before "10.jpg"): the page order.
bool naturalLess(const std::string& a, const std::string& b);
}  // namespace cbz

// A comic book archive: a ZIP of page images shown through the Gray16 plane
// cache. The interface follows Xtc so readers and the home screen treat both
// alike. Cache: <cacheDir>/cbz_<hash>/ with pages/NNNN.g16, cover/thumb BMPs
// and progress.bin.
class Cbz {
 public:
  explicit Cbz(std::string path, const char* cacheDir = "/.crosspoint");

  // Enumerates and sorts the page entries. False when the archive cannot be
  // read or holds no page images.
  bool load();
  bool isLoaded() const { return loaded; }

  const std::string& getPath() const { return filepath; }
  const std::string& getCachePath() const { return cachePath; }
  std::string getTitle() const;  // file name without extension; a CBZ has no author
  uint32_t getPageCount() const { return static_cast<uint32_t>(pages.size()); }
  // Archive entry name of a page in reading order; empty when out of range.
  const std::string& getPageName(uint32_t page) const;
  uint8_t calculateProgress(uint32_t currentPage) const;
  // Whole-book percentage from a page index and the page count (progress.bin
  // stores both, so the home screen needs no archive scan).
  static uint8_t calculateProgress(uint32_t currentPage, uint32_t pageCount);

  void setupCacheDir() const;
  bool clearCache() const;

  // Cover (2-bit, sleep screen) and thumbnail (1-bit, home screen) from the
  // first page, named like the Epub equivalents.
  std::string getCoverBmpPath(bool cropped = false, bool originalThresholds = false) const;
  bool generateCoverBmp(bool cropped = false, bool originalThresholds = false) const;
  std::string getThumbBmpPath() const;  // "[HEIGHT]" placeholder, see UITheme::getCoverThumbPath
  std::string getThumbBmpPath(int height) const;
  bool generateThumbBmp(int height) const;

  // Gray16 plane cache. A plane file is baked for one orientation and panel
  // size; hasPagePlanes() is false for a mismatch and ensurePagePlanes()
  // regenerates it.
  std::string getPagePlanesPath(uint32_t page) const;
  bool readPageSpec(uint32_t page, gray16::PageSpec* out) const;
  bool hasPagePlanes(uint32_t page, uint8_t orientation, uint16_t panelWidth, uint16_t panelHeight) const;
  // extract -> 8-bit BMP fitted to the logical page -> dithered planes. A
  // failure leaves no plane file behind.
  bool ensurePagePlanes(uint32_t page, uint8_t orientation, uint16_t panelWidth, uint16_t panelHeight) const;
  // Streams plane k (0..3) of a page baked for panelWidth x panelHeight into
  // a panel-sized framebuffer; `scratch` holds at least
  // gray16::PageSpec::maxTileBytes() bytes for the page's spec. A header for
  // another panel or a too-small scratch is rejected (file kept); a file whose
  // body cannot be read is removed so the next ensurePagePlanes() rebuilds it.
  bool loadPlane(uint32_t page, uint8_t k, uint16_t panelWidth, uint16_t panelHeight, uint8_t* framebuffer,
                 uint8_t* scratch, size_t scratchLen) const;

 private:
  bool extractPage(uint32_t page, const std::string& outPath) const;
  bool writePlanes(const std::string& bmpPath, const std::string& outPath, gray16::PageSpec spec) const;

  std::string filepath;
  std::string cachePath;
  std::vector<std::string> pages;  // entry names in reading order
  bool loaded = false;
};
