#include <Bitmap.h>
#include <Cbz.h>
#include <ConverterStub.h>
#include <FsHelpers.h>
#include <Gray16.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

void putLe16(std::vector<uint8_t>& out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void putLe32(std::vector<uint8_t>& out, uint32_t value) {
  putLe16(out, static_cast<uint16_t>(value));
  putLe16(out, static_cast<uint16_t>(value >> 16));
}

void putBytes(std::vector<uint8_t>& out, const std::string& bytes) {
  out.insert(out.end(), bytes.begin(), bytes.end());
}

// Minimal zip writer for stored entries: local headers, central directory,
// end of central directory (same shape as the ZipFile test's builder).
class ZipBuilder {
 public:
  ZipBuilder& addStored(const std::string& name, const std::string& content) {
    entries.push_back({name, content, 0});
    return *this;
  }

  std::vector<uint8_t> build() {
    std::vector<uint8_t> out;
    for (auto& entry : entries) {
      entry.localOffset = static_cast<uint32_t>(out.size());
      putLe32(out, 0x04034b50);
      putLe16(out, 20);  // version needed
      putLe16(out, 0);   // flags
      putLe16(out, 0);   // stored
      putLe32(out, 0);   // time + date
      putLe32(out, 0);   // crc, not checked by the reader
      putLe32(out, static_cast<uint32_t>(entry.content.size()));
      putLe32(out, static_cast<uint32_t>(entry.content.size()));
      putLe16(out, static_cast<uint16_t>(entry.name.size()));
      putLe16(out, 0);  // extra length
      putBytes(out, entry.name);
      putBytes(out, entry.content);
    }
    const uint32_t centralDirOffset = static_cast<uint32_t>(out.size());
    for (const auto& entry : entries) {
      putLe32(out, 0x02014b50);
      putLe16(out, 20);  // version made by
      putLe16(out, 20);  // version needed
      putLe16(out, 0);   // flags
      putLe16(out, 0);   // stored
      putLe32(out, 0);   // time + date
      putLe32(out, 0);   // crc
      putLe32(out, static_cast<uint32_t>(entry.content.size()));
      putLe32(out, static_cast<uint32_t>(entry.content.size()));
      putLe16(out, static_cast<uint16_t>(entry.name.size()));
      putLe16(out, 0);  // extra length
      putLe16(out, 0);  // comment length
      putLe16(out, 0);  // disk number start
      putLe16(out, 0);  // internal attributes
      putLe32(out, 0);  // external attributes
      putLe32(out, entry.localOffset);
      putBytes(out, entry.name);
    }
    const uint32_t centralDirSize = static_cast<uint32_t>(out.size()) - centralDirOffset;
    putLe32(out, 0x06054b50);
    putLe16(out, 0);  // this disk
    putLe16(out, 0);  // central directory disk
    putLe16(out, static_cast<uint16_t>(entries.size()));
    putLe16(out, static_cast<uint16_t>(entries.size()));
    putLe32(out, centralDirSize);
    putLe32(out, centralDirOffset);
    putLe16(out, 0);  // comment length
    return out;
  }

 private:
  struct Entry {
    std::string name;
    std::string content;
    uint32_t localOffset;
  };
  std::vector<Entry> entries;
};

class VectorSink : public Print {
 public:
  size_t write(uint8_t byte) override {
    bytes.push_back(byte);
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) override {
    bytes.insert(bytes.end(), data, data + size);
    return size;
  }
  std::vector<uint8_t> bytes;
};

constexpr char BOOK_PATH[] = "/books/My Comic.cbz";

// Pages out of order plus every kind of entry the filter must skip.
std::vector<uint8_t> sampleArchive() {
  return ZipBuilder()
      .addStored("010.jpg", "ten")
      .addStored("__MACOSX/._001.jpeg", "fork")
      .addStored("chapter/", "")
      .addStored("002.png", "two")
      .addStored("001.jpeg", "one")
      .addStored("notes.txt", "text")
      .addStored("003.JPG", "three")
      .build();
}

std::string asText(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

// Synthetic "panel" for the plane cache: 32x16 physical, logical 16x32 in portrait.
constexpr uint16_t PANEL_W = 32;
constexpr uint16_t PANEL_H = 16;

// Row luminances that are exact multiples of 17: levels 15, 0 and 5 with no
// dither error carried, so every plane bit is predictable.
uint8_t stripeLum(int y) {
  static const uint8_t lums[3] = {255, 0, 85};
  return lums[y % 3];
}

bool planeBit(const std::vector<uint8_t>& framebuffer, uint8_t orientation, int x, int y) {
  int px = 0, py = 0;
  gray16::logicalToPhysical(orientation, PANEL_W, PANEL_H, x, y, &px, &py);
  return (framebuffer[py * (PANEL_W / 8) + px / 8] >> (7 - px % 8)) & 1;
}

class CbzTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    converterStub::reset();
  }

  Cbz loadSample() {
    Storage.setFile(BOOK_PATH, sampleArchive());
    Cbz cbz(BOOK_PATH);
    EXPECT_TRUE(cbz.load());
    return cbz;
  }
};

}  // namespace

TEST(CbzIsPageEntry, AcceptsJpegAndPngFiles) {
  EXPECT_TRUE(cbz::isPageEntry("001.jpg"));
  EXPECT_TRUE(cbz::isPageEntry("001.jpeg"));
  EXPECT_TRUE(cbz::isPageEntry("001.png"));
  EXPECT_TRUE(cbz::isPageEntry("Vol1/Page_02.JPEG"));
  EXPECT_TRUE(cbz::isPageEntry("a/b/c.PNG"));
}

TEST(CbzIsPageEntry, RejectsNonPageEntries) {
  EXPECT_FALSE(cbz::isPageEntry(""));
  EXPECT_FALSE(cbz::isPageEntry("chapter/"));
  EXPECT_FALSE(cbz::isPageEntry("__MACOSX/001.jpg"));
  EXPECT_FALSE(cbz::isPageEntry("Vol1/__MACOSX/001.jpg"));
  EXPECT_FALSE(cbz::isPageEntry(".hidden.jpg"));
  EXPECT_FALSE(cbz::isPageEntry("Vol1/._001.jpg"));
  EXPECT_FALSE(cbz::isPageEntry("notes.txt"));
  EXPECT_FALSE(cbz::isPageEntry("ComicInfo.xml"));
  EXPECT_FALSE(cbz::isPageEntry("001.webp"));
  EXPECT_FALSE(cbz::isPageEntry("001.gif"));
  EXPECT_FALSE(cbz::isPageEntry("jpg"));
}

TEST(CbzNaturalLess, DigitRunsCompareNumerically) {
  EXPECT_TRUE(cbz::naturalLess("2.jpg", "10.jpg"));
  EXPECT_FALSE(cbz::naturalLess("10.jpg", "2.jpg"));
  EXPECT_TRUE(cbz::naturalLess("a2", "a10"));
  EXPECT_TRUE(cbz::naturalLess("ch1/p2", "ch1/p10"));
  EXPECT_TRUE(cbz::naturalLess("001.jpg", "002.jpg"));
  EXPECT_TRUE(cbz::naturalLess("page9.jpg", "page10.jpg"));
}

TEST(CbzNaturalLess, IsCaseInsensitive) {
  EXPECT_FALSE(cbz::naturalLess("A.jpg", "a.jpg"));
  EXPECT_FALSE(cbz::naturalLess("a.jpg", "A.jpg"));
  EXPECT_TRUE(cbz::naturalLess("a.jpg", "B.jpg"));
  EXPECT_TRUE(cbz::naturalLess("A.jpg", "b.jpg"));
}

TEST(CbzNaturalLess, EqualStringsOrderNeitherWay) {
  EXPECT_FALSE(cbz::naturalLess("007.jpg", "007.jpg"));
  EXPECT_FALSE(cbz::naturalLess("", ""));
}

TEST(CbzNaturalLess, PrefixesAndMixedNames) {
  EXPECT_TRUE(cbz::naturalLess("page", "page1"));
  EXPECT_FALSE(cbz::naturalLess("page1", "page"));
  EXPECT_TRUE(cbz::naturalLess("cover.jpg", "page1.jpg"));
  EXPECT_TRUE(cbz::naturalLess("ch1/p10", "ch2/p1"));
  EXPECT_TRUE(cbz::naturalLess("", "a"));
}

TEST(CbzNaturalLess, MatchesTheStringOverload) {
  const std::string two = "2.jpg";
  const std::string ten = "10.jpg";
  EXPECT_EQ(cbz::naturalLess(two, ten), FsHelpers::naturalLess(two, ten));
  EXPECT_EQ(cbz::naturalLess(ten, two), FsHelpers::naturalLess(ten, two));
}

TEST_F(CbzTest, LoadSortsPagesNaturallyAndSkipsNonPages) {
  Storage.setFile(BOOK_PATH, sampleArchive());
  Cbz cbz(BOOK_PATH);
  ASSERT_TRUE(cbz.load());
  EXPECT_TRUE(cbz.isLoaded());
  ASSERT_EQ(cbz.getPageCount(), 4u);
  EXPECT_EQ(cbz.getPageName(0), "001.jpeg");
  EXPECT_EQ(cbz.getPageName(1), "002.png");
  EXPECT_EQ(cbz.getPageName(2), "003.JPG");
  EXPECT_EQ(cbz.getPageName(3), "010.jpg");
  EXPECT_EQ(cbz.getPageName(4), "");
  EXPECT_EQ(cbz.getPath(), BOOK_PATH);
  EXPECT_EQ(cbz.getTitle(), "My Comic");
  EXPECT_EQ(cbz.getCachePath().rfind("/.crosspoint/cbz_", 0), 0u);
}

TEST_F(CbzTest, LoadFailsWithoutPageImages) {
  Storage.setFile(BOOK_PATH, ZipBuilder().addStored("readme.txt", "no pages").addStored("art/", "").build());
  Cbz cbz(BOOK_PATH);
  EXPECT_FALSE(cbz.load());
  EXPECT_FALSE(cbz.isLoaded());
  EXPECT_EQ(cbz.getPageCount(), 0u);
}

TEST_F(CbzTest, LoadFailsForMissingArchive) {
  Cbz cbz("/books/missing.cbz");
  EXPECT_FALSE(cbz.load());
  EXPECT_EQ(cbz.getPageCount(), 0u);
}

TEST_F(CbzTest, ProgressAndCachePaths) {
  Cbz cbz = loadSample();
  EXPECT_EQ(cbz.calculateProgress(0), 25);
  EXPECT_EQ(cbz.calculateProgress(3), 100);
  EXPECT_NE(cbz.getThumbBmpPath().find("/thumb_[HEIGHT].bmp"), std::string::npos);
  EXPECT_EQ(cbz.getThumbBmpPath(400), cbz.getCachePath() + "/thumb_400.bmp");
  EXPECT_EQ(cbz.getCoverBmpPath(), cbz.getCachePath() + "/cover_legacy_v2.bmp");
  EXPECT_EQ(cbz.getCoverBmpPath(true, true), cbz.getCachePath() + "/cover_original_crop.bmp");
  EXPECT_EQ(cbz.getPagePlanesPath(7), cbz.getCachePath() + "/pages/0007.g16");
}

TEST_F(CbzTest, ThumbGenerationUsesTheFirstPage) {
  Cbz cbz = loadSample();
  ASSERT_TRUE(cbz.generateThumbBmp(400));
  EXPECT_EQ(converterStub::calls, 1);
  EXPECT_EQ(converterStub::lastMaxWidth, 240);
  EXPECT_EQ(converterStub::lastMaxHeight, 400);
  EXPECT_EQ(asText(converterStub::lastInput), "one");
  EXPECT_NE(Storage.fileBytes(cbz.getThumbBmpPath(400)), nullptr);
  EXPECT_EQ(Storage.fileBytes(cbz.getCachePath() + "/page.tmp"), nullptr);
  // A cached thumbnail is not converted again.
  EXPECT_TRUE(cbz.generateThumbBmp(400));
  EXPECT_EQ(converterStub::calls, 1);
}

TEST_F(CbzTest, ThumbFailureLeavesNoFiles) {
  Cbz cbz = loadSample();
  converterStub::succeed = false;
  EXPECT_FALSE(cbz.generateThumbBmp(400));
  EXPECT_TRUE(Storage.filesUnder(cbz.getCachePath()).empty());
}

TEST_F(CbzTest, CoverGenerationWritesTheVariantFile) {
  Cbz cbz = loadSample();
  ASSERT_TRUE(cbz.generateCoverBmp(true, false));
  EXPECT_EQ(asText(converterStub::lastInput), "one");
  EXPECT_NE(Storage.fileBytes(cbz.getCoverBmpPath(true, false)), nullptr);
  EXPECT_EQ(Storage.fileBytes(cbz.getCoverBmpPath()), nullptr);
  EXPECT_EQ(Storage.fileBytes(cbz.getCachePath() + "/page.tmp"), nullptr);
}

TEST_F(CbzTest, EnsurePagePlanesWritesAndReloadsPlanes) {
  Cbz cbz = loadSample();
  converterStub::rowLum = stripeLum;
  const uint8_t portrait = gray16::Portrait;
  EXPECT_FALSE(cbz.hasPagePlanes(1, portrait, PANEL_W, PANEL_H));

  ASSERT_TRUE(cbz.ensurePagePlanes(1, portrait, PANEL_W, PANEL_H));
  EXPECT_EQ(converterStub::calls, 1);
  EXPECT_EQ(converterStub::lastMaxWidth, 16);  // logical portrait page
  EXPECT_EQ(converterStub::lastMaxHeight, 32);
  EXPECT_EQ(asText(converterStub::lastInput), "two");  // page 1 is 002.png
  EXPECT_TRUE(cbz.hasPagePlanes(1, portrait, PANEL_W, PANEL_H));
  EXPECT_FALSE(cbz.hasPagePlanes(1, gray16::LandscapeCounterClockwise, PANEL_W, PANEL_H));

  // Temp files are gone; only the plane file remains.
  const auto cached = Storage.filesUnder(cbz.getCachePath());
  ASSERT_EQ(cached.size(), 1u);
  EXPECT_EQ(cached[0], cbz.getCachePath() + "/pages/0001.g16");

  gray16::PageSpec spec;
  ASSERT_TRUE(cbz.readPageSpec(1, &spec));
  EXPECT_EQ(spec.imageX, 0);
  EXPECT_EQ(spec.imageY, 0);
  EXPECT_EQ(spec.imageWidth, 16);
  EXPECT_EQ(spec.imageHeight, 32);

  std::vector<uint8_t> framebuffer(PANEL_W * PANEL_H / 8, 0xAA);
  std::vector<uint8_t> scratch(spec.maxTileBytes());
  for (uint8_t k = 0; k < gray16::PLANE_COUNT; k++) {
    ASSERT_TRUE(cbz.loadPlane(1, k, framebuffer.data(), scratch.data(), scratch.size()));
    for (int y = 0; y < 32; y++) {
      const int level = stripeLum(y) / 17;
      for (int x = 0; x < 16; x++) {
        EXPECT_EQ(planeBit(framebuffer, portrait, x, y), (level >> k) & 1)
            << "plane " << int(k) << " at " << x << "," << y;
      }
    }
  }

  // Cached planes are reused; another orientation regenerates at its page size.
  EXPECT_TRUE(cbz.ensurePagePlanes(1, portrait, PANEL_W, PANEL_H));
  EXPECT_EQ(converterStub::calls, 1);
  ASSERT_TRUE(cbz.ensurePagePlanes(1, gray16::LandscapeCounterClockwise, PANEL_W, PANEL_H));
  EXPECT_EQ(converterStub::calls, 2);
  EXPECT_EQ(converterStub::lastMaxWidth, 32);
  EXPECT_EQ(converterStub::lastMaxHeight, 16);
  EXPECT_TRUE(cbz.hasPagePlanes(1, gray16::LandscapeCounterClockwise, PANEL_W, PANEL_H));
  EXPECT_FALSE(cbz.hasPagePlanes(1, portrait, PANEL_W, PANEL_H));
}

TEST_F(CbzTest, SmallerImageIsCentredWithWhiteAround) {
  Cbz cbz = loadSample();
  converterStub::rowLum = stripeLum;
  converterStub::gray8Width = 8;
  converterStub::gray8Height = 8;
  const uint8_t portrait = gray16::Portrait;
  ASSERT_TRUE(cbz.ensurePagePlanes(0, portrait, PANEL_W, PANEL_H));

  gray16::PageSpec spec;
  ASSERT_TRUE(cbz.readPageSpec(0, &spec));
  EXPECT_EQ(spec.imageX, 4);
  EXPECT_EQ(spec.imageY, 12);
  EXPECT_EQ(spec.imageWidth, 8);
  EXPECT_EQ(spec.imageHeight, 8);

  std::vector<uint8_t> framebuffer(PANEL_W * PANEL_H / 8, 0);
  std::vector<uint8_t> scratch(spec.maxTileBytes());
  for (uint8_t k = 0; k < gray16::PLANE_COUNT; k++) {
    ASSERT_TRUE(cbz.loadPlane(0, k, framebuffer.data(), scratch.data(), scratch.size()));
    for (int y = 0; y < 32; y++) {
      for (int x = 0; x < 16; x++) {
        const bool inside = x >= 4 && x < 12 && y >= 12 && y < 20;
        const int level = inside ? stripeLum(y - 12) / 17 : 15;
        EXPECT_EQ(planeBit(framebuffer, portrait, x, y), (level >> k) & 1)
            << "plane " << int(k) << " at " << x << "," << y;
      }
    }
  }
}

TEST_F(CbzTest, OversizedBmpIsClampedToThePage) {
  Cbz cbz = loadSample();
  converterStub::rowLum = stripeLum;
  converterStub::gray8Width = 20;
  converterStub::gray8Height = 40;
  ASSERT_TRUE(cbz.ensurePagePlanes(0, gray16::Portrait, PANEL_W, PANEL_H));
  gray16::PageSpec spec;
  ASSERT_TRUE(cbz.readPageSpec(0, &spec));
  EXPECT_EQ(spec.imageX, 0);
  EXPECT_EQ(spec.imageY, 0);
  EXPECT_EQ(spec.imageWidth, 16);
  EXPECT_EQ(spec.imageHeight, 32);
  std::vector<uint8_t> framebuffer(PANEL_W * PANEL_H / 8, 0);
  std::vector<uint8_t> scratch(spec.maxTileBytes());
  ASSERT_TRUE(cbz.loadPlane(0, 3, framebuffer.data(), scratch.data(), scratch.size()));
  EXPECT_TRUE(planeBit(framebuffer, gray16::Portrait, 0, 0));   // level 15
  EXPECT_FALSE(planeBit(framebuffer, gray16::Portrait, 0, 1));  // level 0
}

TEST_F(CbzTest, EnsurePagePlanesFailureLeavesNoFiles) {
  Cbz cbz = loadSample();
  converterStub::succeed = false;
  EXPECT_FALSE(cbz.ensurePagePlanes(2, gray16::Portrait, PANEL_W, PANEL_H));
  EXPECT_EQ(asText(converterStub::lastInput), "three");
  EXPECT_TRUE(Storage.filesUnder(cbz.getCachePath()).empty());
  EXPECT_FALSE(cbz.hasPagePlanes(2, gray16::Portrait, PANEL_W, PANEL_H));
  EXPECT_FALSE(cbz.ensurePagePlanes(99, gray16::Portrait, PANEL_W, PANEL_H));
}

TEST_F(CbzTest, LoadPlaneRejectsMissingOrInvalidInput) {
  Cbz cbz = loadSample();
  std::vector<uint8_t> framebuffer(PANEL_W * PANEL_H / 8, 0);
  std::vector<uint8_t> scratch(64);
  EXPECT_FALSE(cbz.loadPlane(0, 0, framebuffer.data(), scratch.data(), scratch.size()));
  converterStub::rowLum = stripeLum;
  ASSERT_TRUE(cbz.ensurePagePlanes(0, gray16::Portrait, PANEL_W, PANEL_H));
  EXPECT_FALSE(cbz.loadPlane(0, 4, framebuffer.data(), scratch.data(), scratch.size()));
  EXPECT_FALSE(cbz.loadPlane(9, 0, framebuffer.data(), scratch.data(), scratch.size()));
}

TEST(BitmapLum, ReadsPaletteLuminanceFromGray8Bmp) {
  Storage.reset();
  converterStub::reset();
  converterStub::rowLum = stripeLum;
  VectorSink sink;
  converterStub::writeGray8Bmp(sink, 5, 3);
  Storage.setFile("/gray8.bmp", sink.bytes);

  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("T", std::string("/gray8.bmp"), file));
  Bitmap bitmap(file);
  ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
  EXPECT_TRUE(bitmap.isTopDown());
  EXPECT_EQ(bitmap.getWidth(), 5);
  EXPECT_EQ(bitmap.getHeight(), 3);
  EXPECT_EQ(bitmap.getRowBytes(), 8);
  std::vector<uint8_t> row(bitmap.getRowBytes());
  uint8_t lum[5];
  for (int y = 0; y < 3; y++) {
    ASSERT_EQ(bitmap.readNextRowLum(lum, row.data()), BmpReaderError::Ok);
    for (int x = 0; x < 5; x++) EXPECT_EQ(lum[x], stripeLum(y)) << "row " << y << " x " << x;
  }
  EXPECT_EQ(bitmap.readNextRowLum(lum, row.data()), BmpReaderError::ShortReadRow);
}

TEST(BitmapLum, ConvertsTrueColorAndOneBitRows) {
  Storage.reset();
  // 24-bit, 2x1, top-down: red (B,G,R = 0,0,255) and white, padded to 4 bytes.
  std::vector<uint8_t> rgb;
  putBytes(rgb, "BM");
  putLe32(rgb, 54 + 8);
  putLe32(rgb, 0);
  putLe32(rgb, 54);
  putLe32(rgb, 40);
  putLe32(rgb, 2);
  putLe32(rgb, static_cast<uint32_t>(-1));
  putLe16(rgb, 1);
  putLe16(rgb, 24);
  putLe32(rgb, 0);
  putLe32(rgb, 8);
  putLe32(rgb, 2835);
  putLe32(rgb, 2835);
  putLe32(rgb, 0);
  putLe32(rgb, 0);
  const uint8_t pixels[8] = {0, 0, 255, 255, 255, 255, 0, 0};
  rgb.insert(rgb.end(), pixels, pixels + 8);
  Storage.setFile("/rgb.bmp", rgb);
  HalFile rgbFile;
  ASSERT_TRUE(Storage.openFileForRead("T", std::string("/rgb.bmp"), rgbFile));
  Bitmap rgbBitmap(rgbFile);
  ASSERT_EQ(rgbBitmap.parseHeaders(), BmpReaderError::Ok);
  uint8_t rgbRow[8];
  uint8_t lum[8];
  ASSERT_EQ(rgbBitmap.readNextRowLum(lum, rgbRow), BmpReaderError::Ok);
  EXPECT_EQ(lum[0], (77u * 255) >> 8);  // red weight only
  EXPECT_EQ(lum[1], 255);

  // 1-bit, 8x1, palette black then white: bits 1010 0000.
  std::vector<uint8_t> mono;
  putBytes(mono, "BM");
  putLe32(mono, 62 + 4);
  putLe32(mono, 0);
  putLe32(mono, 62);
  putLe32(mono, 40);
  putLe32(mono, 8);
  putLe32(mono, static_cast<uint32_t>(-1));
  putLe16(mono, 1);
  putLe16(mono, 1);
  putLe32(mono, 0);
  putLe32(mono, 4);
  putLe32(mono, 2835);
  putLe32(mono, 2835);
  putLe32(mono, 2);
  putLe32(mono, 2);
  const uint8_t palette[8] = {0, 0, 0, 0, 255, 255, 255, 0};
  mono.insert(mono.end(), palette, palette + 8);
  const uint8_t monoRow[4] = {0xA0, 0, 0, 0};
  mono.insert(mono.end(), monoRow, monoRow + 4);
  Storage.setFile("/mono.bmp", mono);
  HalFile monoFile;
  ASSERT_TRUE(Storage.openFileForRead("T", std::string("/mono.bmp"), monoFile));
  Bitmap monoBitmap(monoFile);
  ASSERT_EQ(monoBitmap.parseHeaders(), BmpReaderError::Ok);
  uint8_t monoRaw[4];
  ASSERT_EQ(monoBitmap.readNextRowLum(lum, monoRaw), BmpReaderError::Ok);
  const uint8_t expected[8] = {255, 0, 255, 0, 0, 0, 0, 0};
  for (int x = 0; x < 8; x++) EXPECT_EQ(lum[x], expected[x]) << "x " << x;
}
