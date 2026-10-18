#include "Cbz.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <ZipFile.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdio>

namespace {

// Inflate/copy chunk while extracting an entry (two such buffers live during
// a deflated extraction, see ZipFile::readFileToStream).
constexpr size_t EXTRACT_CHUNK_BYTES = 4096;
// Image rows between scheduler yields while writing planes.
constexpr uint16_t YIELD_EVERY_ROWS = 32;

bool halWrite(void* ctx, const uint8_t* data, const size_t len) {
  return static_cast<HalFile*>(ctx)->write(data, len) == len;
}

bool halReadAt(void* ctx, const uint32_t offset, uint8_t* dst, const size_t len) {
  auto* file = static_cast<HalFile*>(ctx);
  return file->seek(offset) && file->read(dst, len) == static_cast<int>(len);
}

enum class BmpOutput : uint8_t { Cover2Bit, Thumb1Bit, Gray8 };

// Decodes the extracted image at `imagePath` into `bmpPath`. The entry name
// selects the decoder; `width`/`height` are the thumb/gray8 target size.
bool convertImage(const std::string& entryName, const std::string& imagePath, const std::string& bmpPath,
                  const BmpOutput output, const int width, const int height, const bool cropped,
                  const bool originalThresholds) {
  const bool jpg = FsHelpers::hasJpgExtension(entryName);
  if (!jpg && !FsHelpers::hasPngExtension(entryName)) return false;

  HalFile image;
  if (!Storage.openFileForRead("CBZ", imagePath, image)) return false;
  HalFile bmp;
  if (!Storage.openFileForWrite("CBZ", bmpPath, bmp)) return false;

  bool ok = false;
  switch (output) {
    case BmpOutput::Cover2Bit:
      ok = jpg ? JpegToBmpConverter::jpegFileToBmpStream(image, bmp, cropped, originalThresholds)
               : PngToBmpConverter::pngFileToBmpStream(image, bmp, cropped, originalThresholds);
      break;
    case BmpOutput::Thumb1Bit:
      ok = jpg ? JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(image, bmp, width, height)
               : PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(image, bmp, width, height);
      break;
    case BmpOutput::Gray8:
      ok = jpg ? JpegToBmpConverter::jpegFileToGray8BmpStreamWithSize(image, bmp, width, height)
               : PngToBmpConverter::pngFileToGray8BmpStreamWithSize(image, bmp, width, height);
      break;
  }
  // The caller removes or reopens both paths next.
  image.close();
  bmp.close();
  if (!ok) {
    LOG_ERR("CBZ", "Cannot decode %s", entryName.c_str());
    Storage.remove(bmpPath.c_str());
  }
  return ok;
}

}  // namespace

namespace cbz {

bool isPageEntry(const std::string_view name) {
  if (name.empty() || name.back() == '/') return false;
  // Walk the path components: reject macOS resource forks anywhere and a
  // hidden (dot) basename.
  size_t start = 0;
  while (true) {
    const size_t slash = name.find('/', start);
    const std::string_view component =
        name.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
    if (component == "__MACOSX") return false;
    if (slash == std::string_view::npos) {
      if (component.empty() || component.front() == '.') return false;
      break;
    }
    start = slash + 1;
  }
  return FsHelpers::hasJpgExtension(name) || FsHelpers::hasPngExtension(name);
}

bool naturalLess(const std::string& a, const std::string& b) { return FsHelpers::naturalLess(a, b); }

}  // namespace cbz

Cbz::Cbz(std::string path, const char* cacheDir) : filepath(std::move(path)) {
  // Same cache key scheme as Epub and Xtc: a hash of the file path.
  cachePath = std::string(cacheDir) + "/cbz_" + std::to_string(std::hash<std::string>{}(filepath));
}

bool Cbz::load() {
  loaded = false;
  pages.clear();

  ZipFile zip(filepath);
  if (!zip.open()) {
    LOG_ERR("CBZ", "Cannot open %s", filepath.c_str());
    return false;
  }

  // Two passes over the central directory: count, then fill a vector sized once.
  size_t count = 0;
  bool collecting = false;
  const auto visit = [this, &count, &collecting](const std::string_view name) {
    if (!cbz::isPageEntry(name)) return;
    if (!collecting) {
      count++;
    } else if (pages.size() < pages.capacity()) {
      pages.emplace_back(name);
    }
  };
  bool ok = zip.enumerateFilePaths(visit);
  if (ok && count > 0) {
    pages.reserve(count);
    collecting = true;
    ok = zip.enumerateFilePaths(visit);
  }
  zip.close();

  if (!ok) {
    LOG_ERR("CBZ", "Cannot read the archive directory of %s", filepath.c_str());
    pages.clear();
    return false;
  }
  if (pages.empty()) {
    LOG_ERR("CBZ", "No page images in %s", filepath.c_str());
    return false;
  }

  std::sort(pages.begin(), pages.end(),
            [](const std::string& a, const std::string& b) { return cbz::naturalLess(a, b); });
  loaded = true;
  LOG_DBG("CBZ", "Loaded %s (%u pages)", filepath.c_str(), static_cast<unsigned>(pages.size()));
  return true;
}

std::string Cbz::getTitle() const { return FsHelpers::getFileNameWithoutExtension(filepath); }

const std::string& Cbz::getPageName(const uint32_t page) const {
  static const std::string kEmpty;
  return page < pages.size() ? pages[page] : kEmpty;
}

uint8_t Cbz::calculateProgress(const uint32_t currentPage) const {
  return calculateProgress(currentPage, static_cast<uint32_t>(pages.size()));
}

uint8_t Cbz::calculateProgress(const uint32_t currentPage, const uint32_t pageCount) {
  if (pageCount == 0) return 0;
  if (currentPage >= pageCount) return 100;
  return static_cast<uint8_t>((currentPage + 1) * 100 / pageCount);
}

void Cbz::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) return;
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') Storage.mkdir(cachePath.substr(0, i).c_str());
  }
  Storage.mkdir(cachePath.c_str());
}

bool Cbz::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) return true;
  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("CBZ", "Failed to clear cache %s", cachePath.c_str());
    return false;
  }
  return true;
}

std::string Cbz::getCoverBmpPath(const bool cropped, const bool originalThresholds) const {
  return cachePath + "/cover" + (originalThresholds ? "_original" : "_legacy_v2") + (cropped ? "_crop" : "") + ".bmp";
}

bool Cbz::generateCoverBmp(const bool cropped, const bool originalThresholds) const {
  const std::string coverPath = getCoverBmpPath(cropped, originalThresholds);
  if (Storage.exists(coverPath.c_str())) return true;
  if (!loaded) {
    LOG_ERR("CBZ", "Cannot generate cover, archive not loaded");
    return false;
  }
  setupCacheDir();
  const std::string imagePath = cachePath + "/page.tmp";
  if (!extractPage(0, imagePath)) return false;
  const bool ok = convertImage(pages[0], imagePath, coverPath, BmpOutput::Cover2Bit, 0, 0, cropped, originalThresholds);
  Storage.remove(imagePath.c_str());
  return ok;
}

std::string Cbz::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }

std::string Cbz::getThumbBmpPath(const int height) const {
  return cachePath + "/thumb_" + std::to_string(height) + ".bmp";
}

bool Cbz::generateThumbBmp(const int height) const {
  const std::string thumbPath = getThumbBmpPath(height);
  if (Storage.exists(thumbPath.c_str())) return true;
  if (!loaded) {
    LOG_ERR("CBZ", "Cannot generate thumb, archive not loaded");
    return false;
  }
  setupCacheDir();
  const std::string imagePath = cachePath + "/page.tmp";
  if (!extractPage(0, imagePath)) return false;
  // Same card proportions as the Epub and Xtc thumbnails.
  const int thumbWidth = static_cast<int>(height * 0.6f);
  const bool ok = convertImage(pages[0], imagePath, thumbPath, BmpOutput::Thumb1Bit, thumbWidth, height, true, false);
  Storage.remove(imagePath.c_str());
  return ok;
}

std::string Cbz::getPagePlanesPath(const uint32_t page) const {
  char name[24];
  snprintf(name, sizeof(name), "/pages/%04u.g16", static_cast<unsigned>(page));
  return cachePath + name;
}

bool Cbz::readPageSpec(const uint32_t page, gray16::PageSpec* out) const {
  const std::string path = getPagePlanesPath(page);
  if (!Storage.exists(path.c_str())) return false;
  HalFile file;
  if (!Storage.openFileForRead("CBZ", path, file)) return false;
  gray16::Reader reader;
  if (!reader.open(halReadAt, &file)) return false;
  *out = reader.spec();
  return true;
}

bool Cbz::hasPagePlanes(const uint32_t page, const uint8_t orientation, const uint16_t panelWidth,
                        const uint16_t panelHeight) const {
  gray16::PageSpec spec;
  if (!readPageSpec(page, &spec)) return false;
  return spec.orientation == orientation && spec.panelWidth == panelWidth && spec.panelHeight == panelHeight &&
         spec.bandRows == gray16::DEFAULT_BAND_ROWS;
}

bool Cbz::ensurePagePlanes(const uint32_t page, const uint8_t orientation, const uint16_t panelWidth,
                           const uint16_t panelHeight) const {
  if (!loaded || page >= pages.size()) return false;
  if (hasPagePlanes(page, orientation, panelWidth, panelHeight)) return true;

  gray16::PageSpec spec;
  spec.orientation = orientation;
  spec.panelWidth = panelWidth;
  spec.panelHeight = panelHeight;
  gray16::PageSpec::logicalSize(orientation, panelWidth, panelHeight, &spec.pageWidth, &spec.pageHeight);

  setupCacheDir();
  Storage.mkdir((cachePath + "/pages").c_str());
  const std::string imagePath = cachePath + "/page.tmp";
  const std::string bmpPath = cachePath + "/page.bmp";
  const std::string planesPath = getPagePlanesPath(page);
  const std::string tmpPlanesPath = planesPath + ".tmp";
  // Leftovers of an interrupted build.
  Storage.remove(imagePath.c_str());
  Storage.remove(bmpPath.c_str());

  // The extracted image and the BMP are not needed at the same time as the
  // planes, so each is removed as soon as the next step has consumed it.
  bool ok = extractPage(page, imagePath) && convertImage(pages[page], imagePath, bmpPath, BmpOutput::Gray8,
                                                         spec.pageWidth, spec.pageHeight, false, false);
  Storage.remove(imagePath.c_str());
  if (ok) ok = writePlanes(bmpPath, tmpPlanesPath, spec);
  Storage.remove(bmpPath.c_str());
  // Rename last so a power loss never leaves a truncated plane file in place.
  if (ok && !Storage.replaceFile(tmpPlanesPath.c_str(), planesPath.c_str())) {
    LOG_ERR("CBZ", "Cannot move planes into place: %s", planesPath.c_str());
    ok = false;
  }
  if (!ok) {
    Storage.remove(tmpPlanesPath.c_str());
    LOG_ERR("CBZ", "Page %u of %s could not be prepared", static_cast<unsigned>(page + 1), filepath.c_str());
  }
  return ok;
}

bool Cbz::loadPlane(const uint32_t page, const uint8_t k, const uint16_t panelWidth, const uint16_t panelHeight,
                    uint8_t* framebuffer, uint8_t* scratch, const size_t scratchLen) const {
  if (page >= pages.size() || k >= gray16::PLANE_COUNT) return false;
  const std::string path = getPagePlanesPath(page);
  HalFile file;
  if (!Storage.openFileForRead("CBZ", path, file)) return false;
  gray16::Reader reader;
  if (!reader.open(halReadAt, &file)) {
    LOG_ERR("CBZ", "Bad plane file for page %u", static_cast<unsigned>(page + 1));
    return false;
  }
  // Never stream a plane sized for another panel into this framebuffer.
  if (reader.spec().panelWidth != panelWidth || reader.spec().panelHeight != panelHeight) {
    LOG_ERR("CBZ", "Plane file for page %u is for a %ux%u panel", static_cast<unsigned>(page + 1),
            reader.spec().panelWidth, reader.spec().panelHeight);
    return false;
  }
  if (!framebuffer || !scratch || scratchLen < reader.spec().maxTileBytes()) {
    LOG_ERR("CBZ", "Plane scratch too small for page %u", static_cast<unsigned>(page + 1));
    return false;
  }
  if (!reader.loadPlane(k, framebuffer, scratch, scratchLen)) {
    // Valid header, unreadable body (truncated or damaged): drop the file so
    // the next view rebuilds it instead of failing forever.
    file.close();
    Storage.remove(path.c_str());
    LOG_ERR("CBZ", "Plane %u read failed for page %u; planes removed", k, static_cast<unsigned>(page + 1));
    return false;
  }
  return true;
}

bool Cbz::extractPage(const uint32_t page, const std::string& outPath) const {
  HalFile out;
  if (!Storage.openFileForWrite("CBZ", outPath, out)) return false;
  const bool ok = ZipFile(filepath).readFileToStream(pages[page].c_str(), out, EXTRACT_CHUNK_BYTES);
  out.close();  // the caller reopens or removes the path next
  if (!ok) {
    LOG_ERR("CBZ", "Cannot extract %s", pages[page].c_str());
    Storage.remove(outPath.c_str());
  }
  return ok;
}

bool Cbz::writePlanes(const std::string& bmpPath, const std::string& outPath, gray16::PageSpec spec) const {
  HalFile bmpFile;
  if (!Storage.openFileForRead("CBZ", bmpPath, bmpFile)) return false;
  // Heap: the palette table alone exceeds the stack budget for locals.
  const auto bmp = makeUniqueNoThrow<Bitmap>(bmpFile);
  if (!bmp) {
    LOG_ERR("CBZ", "OOM: BMP reader (%u bytes)", static_cast<unsigned>(sizeof(Bitmap)));
    return false;
  }
  const BmpReaderError err = bmp->parseHeaders();
  if (err != BmpReaderError::Ok) {
    LOG_ERR("CBZ", "Page BMP: %s", Bitmap::errorToString(err));
    return false;
  }
  if (!bmp->isTopDown()) {
    // The gray8 converters write top-down rows; the writer streams in that order.
    LOG_ERR("CBZ", "Page BMP is bottom-up");
    return false;
  }

  // The converter already fitted the image to the page; centre it as-is.
  // Clamping guards the writer against a one-pixel rounding overshoot.
  spec.imageWidth = static_cast<uint16_t>(std::min<int>(bmp->getWidth(), spec.pageWidth));
  spec.imageHeight = static_cast<uint16_t>(std::min<int>(bmp->getHeight(), spec.pageHeight));
  spec.imageX = static_cast<uint16_t>((spec.pageWidth - spec.imageWidth) / 2);
  spec.imageY = static_cast<uint16_t>((spec.pageHeight - spec.imageHeight) / 2);

  gray16::Writer writer(spec);
  if (!writer.isValid()) {
    LOG_ERR("CBZ", "OOM: Gray16 writer for a %ux%u page", spec.pageWidth, spec.pageHeight);
    return false;
  }

  // One BMP row as stored plus one luminance byte per pixel.
  const size_t rowBytes = static_cast<size_t>(bmp->getRowBytes());
  const size_t lumBytes = static_cast<size_t>(bmp->getWidth());
  const auto rows = makeUniqueNoThrow<uint8_t[]>(rowBytes + lumBytes);
  if (!rows) {
    LOG_ERR("CBZ", "OOM: row buffers (%u bytes)", static_cast<unsigned>(rowBytes + lumBytes));
    return false;
  }
  uint8_t* rawRow = rows.get();
  uint8_t* lumRow = rawRow + rowBytes;

  HalFile out;
  if (!Storage.openFileForWrite("CBZ", outPath, out)) return false;
  if (!writer.begin(halWrite, &out)) {
    LOG_ERR("CBZ", "Plane header write failed");
    return false;
  }
  for (uint16_t y = 0; y < spec.imageHeight; y++) {
    if (bmp->readNextRowLum(lumRow, rawRow) != BmpReaderError::Ok) {
      LOG_ERR("CBZ", "Short read in page BMP at row %u", y);
      return false;
    }
    if (!writer.addImageRow(lumRow)) {
      LOG_ERR("CBZ", "Plane write failed at row %u", y);
      return false;
    }
    if (y % YIELD_EVERY_ROWS == YIELD_EVERY_ROWS - 1) vTaskDelay(1);
  }
  if (!writer.finish()) {
    LOG_ERR("CBZ", "Plane write failed while finishing");
    return false;
  }
  return true;
}
