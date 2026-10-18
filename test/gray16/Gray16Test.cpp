#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "Gray16.h"

using namespace gray16;

namespace {

constexpr uint8_t ALL_ORIENTATIONS[] = {Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise};

struct VecSink {
  std::vector<uint8_t> bytes;
  bool fail = false;
};

bool writeToVec(void* ctx, const uint8_t* data, size_t len) {
  auto* sink = static_cast<VecSink*>(ctx);
  if (sink->fail) return false;
  sink->bytes.insert(sink->bytes.end(), data, data + len);
  return true;
}

bool readFromVec(void* ctx, uint32_t offset, uint8_t* dst, size_t len) {
  const auto* bytes = static_cast<const std::vector<uint8_t>*>(ctx);
  if (offset > bytes->size() || len > bytes->size() - offset) return false;
  memcpy(dst, bytes->data() + offset, len);
  return true;
}

// Spec whose image covers the whole logical page.
PageSpec fullPageSpec(uint8_t orientation, uint16_t panelW, uint16_t panelH, uint16_t bandRows) {
  PageSpec spec;
  spec.orientation = orientation;
  spec.panelWidth = panelW;
  spec.panelHeight = panelH;
  spec.bandRows = bandRows;
  PageSpec::logicalSize(orientation, panelW, panelH, &spec.pageWidth, &spec.pageHeight);
  spec.imageX = 0;
  spec.imageY = 0;
  spec.imageWidth = spec.pageWidth;
  spec.imageHeight = spec.pageHeight;
  return spec;
}

// X4 page with a letterboxed image: 480x720 at (0, 40) in portrait, 480x480
// at (160, 0) in landscape.
PageSpec x4Spec(uint8_t orientation) {
  PageSpec spec = fullPageSpec(orientation, 800, 480, DEFAULT_BAND_ROWS);
  const bool portrait = orientation == Portrait || orientation == PortraitInverted;
  spec.imageX = portrait ? 0 : 160;
  spec.imageY = portrait ? 40 : 0;
  spec.imageWidth = 480;
  spec.imageHeight = portrait ? 720 : 480;
  return spec;
}

uint32_t fileSize(const PageSpec& spec) {
  uint32_t tiles = 0;
  for (uint16_t band = 0; band < spec.bandCount(); ++band) {
    Rect tile;
    EXPECT_TRUE(spec.tileRect(band, &tile));
    tiles += (tile.w / 8) * tile.h;
  }
  return static_cast<uint32_t>(HEADER_SIZE) + PLANE_COUNT * tiles;
}

uint8_t planeBit(const uint8_t* fb, const PageSpec& spec, int phyX, int phyY) {
  const size_t index = static_cast<size_t>(phyY) * (spec.panelWidth / 8) + static_cast<size_t>(phyX) / 8;
  return (fb[index] >> (7 - (phyX % 8))) & 1;
}

// Brightness of logical (x, y), reconstructed pixel by pixel through
// logicalToPhysical (the reference for the writer's per-row fast path).
uint8_t brightnessAt(const std::vector<std::vector<uint8_t>>& planes, const PageSpec& spec, int x, int y) {
  int phyX = 0;
  int phyY = 0;
  logicalToPhysical(spec.orientation, spec.panelWidth, spec.panelHeight, x, y, &phyX, &phyY);
  uint8_t g = 0;
  for (uint8_t k = 0; k < PLANE_COUNT; ++k)
    g |= static_cast<uint8_t>(planeBit(planes[k].data(), spec, phyX, phyY) << k);
  return g;
}

// Writes one page through the Writer; `lumAt` is evaluated in image coordinates.
template <typename LumFn>
bool writePage(const PageSpec& spec, LumFn lumAt, VecSink* sink) {
  Writer writer(spec);
  if (!writer.isValid()) return false;
  if (!writer.begin(writeToVec, sink)) return false;
  std::vector<uint8_t> row(spec.imageWidth);
  for (uint16_t y = 0; y < spec.imageHeight; ++y) {
    for (uint16_t x = 0; x < spec.imageWidth; ++x) row[x] = lumAt(x, y);
    if (!writer.addImageRow(row.data())) return false;
  }
  return writer.finish();
}

// Reads all four planes back into panel-sized framebuffers pre-filled with
// 0xA5 so untouched bytes stand out.
bool readPlanes(const std::vector<uint8_t>& file, PageSpec* spec, std::vector<std::vector<uint8_t>>* planes) {
  Reader reader;
  if (!reader.open(readFromVec, const_cast<std::vector<uint8_t>*>(&file))) return false;
  *spec = reader.spec();
  const size_t fbBytes = static_cast<size_t>(spec->panelWidth / 8) * spec->panelHeight;
  std::vector<uint8_t> scratch(reader.spec().maxTileBytes());
  planes->assign(PLANE_COUNT, std::vector<uint8_t>(fbBytes, 0xA5));
  for (uint8_t k = 0; k < PLANE_COUNT; ++k) {
    if (!reader.loadPlane(k, (*planes)[k].data(), scratch.data(), scratch.size())) return false;
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Header

TEST(Gray16Header, EncodeDecodeRoundTrip) {
  const PageSpec spec = x4Spec(Portrait);
  ASSERT_TRUE(spec.valid());
  uint8_t bytes[HEADER_SIZE];
  encodeHeader(spec, bytes);
  EXPECT_EQ(memcmp(bytes, MAGIC, sizeof(MAGIC)), 0);
  EXPECT_EQ(bytes[4], VERSION);
  EXPECT_EQ(bytes[5], Portrait);
  EXPECT_EQ(bytes[6], 0x20);  // 800 little-endian
  EXPECT_EQ(bytes[7], 0x03);
  EXPECT_EQ(bytes[8], 0xE0);  // 480
  EXPECT_EQ(bytes[9], 0x01);
  EXPECT_EQ(bytes[10], 64);
  EXPECT_EQ(bytes[11], 0);
  EXPECT_EQ(bytes[18], 40);  // imageY
  for (size_t i = 24; i < HEADER_SIZE; ++i) EXPECT_EQ(bytes[i], 0) << "reserved byte " << i;

  PageSpec decoded;
  ASSERT_TRUE(decodeHeader(bytes, &decoded));
  EXPECT_EQ(decoded.orientation, spec.orientation);
  EXPECT_EQ(decoded.panelWidth, spec.panelWidth);
  EXPECT_EQ(decoded.panelHeight, spec.panelHeight);
  EXPECT_EQ(decoded.bandRows, spec.bandRows);
  EXPECT_EQ(decoded.pageWidth, spec.pageWidth);
  EXPECT_EQ(decoded.pageHeight, spec.pageHeight);
  EXPECT_EQ(decoded.imageX, spec.imageX);
  EXPECT_EQ(decoded.imageY, spec.imageY);
  EXPECT_EQ(decoded.imageWidth, spec.imageWidth);
  EXPECT_EQ(decoded.imageHeight, spec.imageHeight);
}

TEST(Gray16Header, ReservedBytesAreIgnoredOnDecode) {
  uint8_t bytes[HEADER_SIZE];
  encodeHeader(x4Spec(LandscapeClockwise), bytes);
  memset(bytes + 24, 0xEE, HEADER_SIZE - 24);
  PageSpec decoded;
  EXPECT_TRUE(decodeHeader(bytes, &decoded));
  EXPECT_EQ(decoded.orientation, LandscapeClockwise);
}

TEST(Gray16Header, RejectsBadMagicAndVersion) {
  uint8_t bytes[HEADER_SIZE];
  encodeHeader(x4Spec(Portrait), bytes);
  PageSpec decoded;

  uint8_t badMagic[HEADER_SIZE];
  memcpy(badMagic, bytes, HEADER_SIZE);
  badMagic[0] = 'g';
  EXPECT_FALSE(decodeHeader(badMagic, &decoded));

  uint8_t badVersion[HEADER_SIZE];
  memcpy(badVersion, bytes, HEADER_SIZE);
  badVersion[4] = 2;
  EXPECT_FALSE(decodeHeader(badVersion, &decoded));

  EXPECT_FALSE(decodeHeader(nullptr, &decoded));
  EXPECT_FALSE(decodeHeader(bytes, nullptr));
}

TEST(Gray16Header, RejectsInvalidSpecsAndLeavesOutputUntouched) {
  PageSpec decoded;
  decoded.panelWidth = 1234;  // sentinel: must survive a failed decode
  uint8_t bytes[HEADER_SIZE];

  PageSpec portraitMismatch = x4Spec(Portrait);
  portraitMismatch.pageWidth = 800;  // portrait page must be panelHeight wide
  portraitMismatch.pageHeight = 480;
  EXPECT_FALSE(portraitMismatch.valid());
  encodeHeader(portraitMismatch, bytes);
  EXPECT_FALSE(decodeHeader(bytes, &decoded));

  PageSpec oddBand = x4Spec(Portrait);
  oddBand.bandRows = 12;
  EXPECT_FALSE(oddBand.valid());
  encodeHeader(oddBand, bytes);
  EXPECT_FALSE(decodeHeader(bytes, &decoded));

  PageSpec imageOutside = x4Spec(Portrait);
  imageOutside.imageY = 100;  // 100 + 720 > 800
  EXPECT_FALSE(imageOutside.valid());
  encodeHeader(imageOutside, bytes);
  EXPECT_FALSE(decodeHeader(bytes, &decoded));

  EXPECT_EQ(decoded.panelWidth, 1234);
}

TEST(Gray16Spec, ValidityRules) {
  for (uint8_t orientation : ALL_ORIENTATIONS) {
    EXPECT_TRUE(x4Spec(orientation).valid()) << "orientation " << int{orientation};
  }
  PageSpec spec = x4Spec(Portrait);
  spec.orientation = 4;
  EXPECT_FALSE(spec.valid());

  spec = x4Spec(Portrait);
  spec.panelWidth = 804;  // not a multiple of 8
  spec.pageHeight = 804;
  EXPECT_FALSE(spec.valid());

  spec = x4Spec(Portrait);
  spec.bandRows = 0;
  EXPECT_FALSE(spec.valid());

  spec = x4Spec(Portrait);
  spec.imageX = 1;  // 1 + 480 > 480
  EXPECT_FALSE(spec.valid());

  spec = x4Spec(Portrait);
  spec.imageWidth = 0;  // an empty image is still a valid page
  spec.imageHeight = 0;
  EXPECT_TRUE(spec.valid());

  spec = x4Spec(LandscapeCounterClockwise);
  spec.pageWidth = 480;  // landscape page must equal the panel
  spec.pageHeight = 800;
  EXPECT_FALSE(spec.valid());

  PageSpec zero;
  EXPECT_FALSE(zero.valid());
}

TEST(Gray16Spec, LogicalSizeFollowsOrientation) {
  uint16_t w = 0;
  uint16_t h = 0;
  PageSpec::logicalSize(Portrait, 800, 480, &w, &h);
  EXPECT_EQ(w, 480);
  EXPECT_EQ(h, 800);
  PageSpec::logicalSize(PortraitInverted, 800, 480, &w, &h);
  EXPECT_EQ(w, 480);
  EXPECT_EQ(h, 800);
  PageSpec::logicalSize(LandscapeClockwise, 800, 480, &w, &h);
  EXPECT_EQ(w, 800);
  EXPECT_EQ(h, 480);
  PageSpec::logicalSize(LandscapeCounterClockwise, 800, 480, &w, &h);
  EXPECT_EQ(w, 800);
  EXPECT_EQ(h, 480);
}

// ---------------------------------------------------------------------------
// fitImage

TEST(Gray16FitImage, LandscapeSourceOnPortraitPageIsLetterboxed) {
  PageSpec spec = x4Spec(Portrait);
  ASSERT_TRUE(fitImage(1600, 1200, &spec));
  EXPECT_EQ(spec.imageWidth, 480);
  EXPECT_EQ(spec.imageHeight, 360);
  EXPECT_EQ(spec.imageX, 0);
  EXPECT_EQ(spec.imageY, 220);
  EXPECT_TRUE(spec.valid());
}

TEST(Gray16FitImage, PortraitSourceFillsTheWidth) {
  PageSpec spec = x4Spec(Portrait);
  ASSERT_TRUE(fitImage(600, 900, &spec));
  EXPECT_EQ(spec.imageWidth, 480);
  EXPECT_EQ(spec.imageHeight, 720);
  EXPECT_EQ(spec.imageX, 0);
  EXPECT_EQ(spec.imageY, 40);
  EXPECT_TRUE(spec.valid());
}

TEST(Gray16FitImage, TallSourceFillsTheHeight) {
  PageSpec spec = x4Spec(Portrait);
  ASSERT_TRUE(fitImage(100, 400, &spec));
  EXPECT_EQ(spec.imageWidth, 200);
  EXPECT_EQ(spec.imageHeight, 800);
  EXPECT_EQ(spec.imageX, 140);
  EXPECT_EQ(spec.imageY, 0);
  EXPECT_TRUE(spec.valid());
}

TEST(Gray16FitImage, TinySourceIsScaledUp) {
  PageSpec spec = x4Spec(Portrait);
  ASSERT_TRUE(fitImage(2, 3, &spec));
  EXPECT_EQ(spec.imageWidth, 480);
  EXPECT_EQ(spec.imageHeight, 720);
  EXPECT_EQ(spec.imageX, 0);
  EXPECT_EQ(spec.imageY, 40);
}

TEST(Gray16FitImage, SquareSourceOnLandscapePageIsCentred) {
  PageSpec spec = x4Spec(LandscapeCounterClockwise);
  ASSERT_TRUE(fitImage(1000, 1000, &spec));
  EXPECT_EQ(spec.imageWidth, 480);
  EXPECT_EQ(spec.imageHeight, 480);
  EXPECT_EQ(spec.imageX, 160);
  EXPECT_EQ(spec.imageY, 0);
  EXPECT_TRUE(spec.valid());
}

TEST(Gray16FitImage, RejectsZeroDimensions) {
  PageSpec spec = x4Spec(Portrait);
  const PageSpec before = spec;
  EXPECT_FALSE(fitImage(0, 100, &spec));
  EXPECT_FALSE(fitImage(100, 0, &spec));
  EXPECT_FALSE(fitImage(100, 100, nullptr));
  PageSpec noPage;
  EXPECT_FALSE(fitImage(100, 100, &noPage));
  EXPECT_EQ(spec.imageWidth, before.imageWidth);
  EXPECT_EQ(spec.imageHeight, before.imageHeight);
  EXPECT_EQ(spec.imageX, before.imageX);
  EXPECT_EQ(spec.imageY, before.imageY);
}

TEST(Gray16FitImage, ExtremeAspectRatiosKeepAtLeastOnePixel) {
  PageSpec spec = x4Spec(Portrait);
  ASSERT_TRUE(fitImage(1, 65535, &spec));
  EXPECT_EQ(spec.imageWidth, 1);
  EXPECT_EQ(spec.imageHeight, 800);
  EXPECT_TRUE(spec.valid());
  ASSERT_TRUE(fitImage(65535, 1, &spec));
  EXPECT_EQ(spec.imageWidth, 480);
  EXPECT_EQ(spec.imageHeight, 1);
  EXPECT_TRUE(spec.valid());
}

// ---------------------------------------------------------------------------
// Geometry

TEST(Gray16Geometry, LogicalToPhysicalMatchesRotateCoordinates) {
  int px = 0;
  int py = 0;
  logicalToPhysical(Portrait, 800, 480, 0, 0, &px, &py);
  EXPECT_EQ(px, 0);
  EXPECT_EQ(py, 479);
  logicalToPhysical(Portrait, 800, 480, 479, 799, &px, &py);
  EXPECT_EQ(px, 799);
  EXPECT_EQ(py, 0);
  logicalToPhysical(Portrait, 800, 480, 10, 20, &px, &py);
  EXPECT_EQ(px, 20);
  EXPECT_EQ(py, 469);

  logicalToPhysical(LandscapeClockwise, 800, 480, 0, 0, &px, &py);
  EXPECT_EQ(px, 799);
  EXPECT_EQ(py, 479);
  logicalToPhysical(LandscapeClockwise, 800, 480, 10, 20, &px, &py);
  EXPECT_EQ(px, 789);
  EXPECT_EQ(py, 459);

  logicalToPhysical(PortraitInverted, 800, 480, 0, 0, &px, &py);
  EXPECT_EQ(px, 799);
  EXPECT_EQ(py, 0);
  logicalToPhysical(PortraitInverted, 800, 480, 479, 799, &px, &py);
  EXPECT_EQ(px, 0);
  EXPECT_EQ(py, 479);
  logicalToPhysical(PortraitInverted, 800, 480, 10, 20, &px, &py);
  EXPECT_EQ(px, 779);
  EXPECT_EQ(py, 10);

  logicalToPhysical(LandscapeCounterClockwise, 800, 480, 10, 20, &px, &py);
  EXPECT_EQ(px, 10);
  EXPECT_EQ(py, 20);
}

TEST(Gray16Geometry, TileRectsOnSmallPanel) {
  // Panel 32x16, bandRows 8: portrait pages are 16x32 (4 bands), landscape 32x16 (2 bands).
  Rect tile;
  const PageSpec portrait = fullPageSpec(Portrait, 32, 16, 8);
  ASSERT_EQ(portrait.bandCount(), 4);
  for (uint16_t band = 0; band < 4; ++band) {
    ASSERT_TRUE(portrait.tileRect(band, &tile));
    EXPECT_EQ(tile.x, 8 * band);
    EXPECT_EQ(tile.y, 0);
    EXPECT_EQ(tile.w, 8);
    EXPECT_EQ(tile.h, 16);
  }
  EXPECT_FALSE(portrait.tileRect(4, &tile));
  EXPECT_FALSE(portrait.tileRect(0, nullptr));
  EXPECT_EQ(portrait.maxTileBytes(), 16u);

  const PageSpec inverted = fullPageSpec(PortraitInverted, 32, 16, 8);
  ASSERT_EQ(inverted.bandCount(), 4);
  for (uint16_t band = 0; band < 4; ++band) {
    ASSERT_TRUE(inverted.tileRect(band, &tile));
    EXPECT_EQ(tile.x, 24 - 8 * band);
    EXPECT_EQ(tile.y, 0);
    EXPECT_EQ(tile.w, 8);
    EXPECT_EQ(tile.h, 16);
  }

  const PageSpec ccw = fullPageSpec(LandscapeCounterClockwise, 32, 16, 8);
  ASSERT_EQ(ccw.bandCount(), 2);
  for (uint16_t band = 0; band < 2; ++band) {
    ASSERT_TRUE(ccw.tileRect(band, &tile));
    EXPECT_EQ(tile.x, 0);
    EXPECT_EQ(tile.y, 8 * band);
    EXPECT_EQ(tile.w, 32);
    EXPECT_EQ(tile.h, 8);
  }
  EXPECT_FALSE(ccw.tileRect(2, &tile));
  EXPECT_EQ(ccw.maxTileBytes(), 32u);

  const PageSpec cw = fullPageSpec(LandscapeClockwise, 32, 16, 8);
  ASSERT_EQ(cw.bandCount(), 2);
  for (uint16_t band = 0; band < 2; ++band) {
    ASSERT_TRUE(cw.tileRect(band, &tile));
    EXPECT_EQ(tile.x, 0);
    EXPECT_EQ(tile.y, 8 - 8 * band);
    EXPECT_EQ(tile.w, 32);
    EXPECT_EQ(tile.h, 8);
  }
}

TEST(Gray16Geometry, LastPartialBandPortrait) {
  // Panel 40x16, bandRows 16: portrait page 16x40 -> bands of 16, 16, 8 rows.
  Rect tile;
  const PageSpec portrait = fullPageSpec(Portrait, 40, 16, 16);
  ASSERT_EQ(portrait.bandCount(), 3);
  const uint16_t expectedX[] = {0, 16, 32};
  const uint16_t expectedW[] = {16, 16, 8};
  for (uint16_t band = 0; band < 3; ++band) {
    ASSERT_TRUE(portrait.tileRect(band, &tile));
    EXPECT_EQ(tile.x, expectedX[band]);
    EXPECT_EQ(tile.y, 0);
    EXPECT_EQ(tile.w, expectedW[band]);
    EXPECT_EQ(tile.h, 16);
  }
  EXPECT_EQ(portrait.maxTileBytes(), 32u);

  const PageSpec inverted = fullPageSpec(PortraitInverted, 40, 16, 16);
  const uint16_t invertedX[] = {24, 8, 0};
  for (uint16_t band = 0; band < 3; ++band) {
    ASSERT_TRUE(inverted.tileRect(band, &tile));
    EXPECT_EQ(tile.x, invertedX[band]);
    EXPECT_EQ(tile.w, expectedW[band]);
    EXPECT_EQ(tile.h, 16);
  }
}

TEST(Gray16Geometry, LastPartialBandLandscape) {
  // Panel 32x24, bandRows 16: landscape page 32x24 -> bands of 16 and 8 rows.
  Rect tile;
  const PageSpec ccw = fullPageSpec(LandscapeCounterClockwise, 32, 24, 16);
  ASSERT_EQ(ccw.bandCount(), 2);
  ASSERT_TRUE(ccw.tileRect(0, &tile));
  EXPECT_EQ(tile.y, 0);
  EXPECT_EQ(tile.h, 16);
  ASSERT_TRUE(ccw.tileRect(1, &tile));
  EXPECT_EQ(tile.y, 16);
  EXPECT_EQ(tile.h, 8);
  EXPECT_EQ(tile.w, 32);
  EXPECT_EQ(ccw.maxTileBytes(), 64u);

  const PageSpec cw = fullPageSpec(LandscapeClockwise, 32, 24, 16);
  ASSERT_TRUE(cw.tileRect(0, &tile));
  EXPECT_EQ(tile.y, 8);
  EXPECT_EQ(tile.h, 16);
  ASSERT_TRUE(cw.tileRect(1, &tile));
  EXPECT_EQ(tile.y, 0);
  EXPECT_EQ(tile.h, 8);
}

TEST(Gray16Geometry, X4TileSizesMatchTheDesignDoc) {
  EXPECT_EQ(x4Spec(Portrait).maxTileBytes(), 3840u);  // 15 KB band
  EXPECT_EQ(x4Spec(Portrait).bandCount(), 13);        // ceil(800 / 64)
  EXPECT_EQ(x4Spec(LandscapeClockwise).maxTileBytes(), 6400u);
  EXPECT_EQ(x4Spec(LandscapeClockwise).bandCount(), 8);  // 480 / 64 + partial 32
}

// Every logical pixel lands inside its band's tile, and the tiles of a page
// cover every physical pixel exactly once.
TEST(Gray16Geometry, TilesPartitionThePanel) {
  const PageSpec specs[] = {
      fullPageSpec(Portrait, 40, 16, 16),
      fullPageSpec(PortraitInverted, 40, 16, 16),
      fullPageSpec(LandscapeClockwise, 32, 24, 16),
      fullPageSpec(LandscapeCounterClockwise, 32, 24, 16),
  };
  for (const PageSpec& spec : specs) {
    SCOPED_TRACE(int{spec.orientation});
    std::vector<int> hits(static_cast<size_t>(spec.panelWidth) * spec.panelHeight, 0);
    for (int y = 0; y < spec.pageHeight; ++y) {
      Rect tile;
      ASSERT_TRUE(spec.tileRect(static_cast<uint16_t>(y / spec.bandRows), &tile));
      for (int x = 0; x < spec.pageWidth; ++x) {
        int px = 0;
        int py = 0;
        logicalToPhysical(spec.orientation, spec.panelWidth, spec.panelHeight, x, y, &px, &py);
        ASSERT_GE(px, tile.x);
        ASSERT_LT(px, tile.x + tile.w);
        ASSERT_GE(py, tile.y);
        ASSERT_LT(py, tile.y + tile.h);
        ++hits[static_cast<size_t>(py) * spec.panelWidth + static_cast<size_t>(px)];
      }
    }
    for (int hit : hits) EXPECT_EQ(hit, 1);
  }
}

// ---------------------------------------------------------------------------
// Ditherer

TEST(Gray16Ditherer, MidGrayAveragesBetweenSevenAndEight) {
  Ditherer ditherer(64);
  ASSERT_TRUE(ditherer.isValid());
  long sum = 0;
  for (int y = 0; y < 16; ++y) {
    for (uint16_t x = 0; x < 64; ++x) {
      const uint8_t g = ditherer.processPixel(128, x);
      ASSERT_LE(g, 15);
      sum += g;
    }
    ditherer.nextRow();
  }
  const double mean = static_cast<double>(sum) / (16 * 64);
  EXPECT_GT(mean, 7.0);
  EXPECT_LT(mean, 8.0);
}

TEST(Gray16Ditherer, OutputStaysInRangeAndIsDeterministic) {
  Ditherer a(97);
  Ditherer b(97);
  ASSERT_TRUE(a.isValid());
  ASSERT_TRUE(b.isValid());
  uint32_t seed = 12345;
  for (int y = 0; y < 40; ++y) {
    for (uint16_t x = 0; x < 97; ++x) {
      seed = seed * 1664525u + 1013904223u;
      const uint8_t lum = static_cast<uint8_t>(seed >> 24);
      const uint8_t ga = a.processPixel(lum, x);
      const uint8_t gb = b.processPixel(lum, x);
      ASSERT_LE(ga, 15);
      ASSERT_EQ(ga, gb) << "x=" << x << " y=" << y;
    }
    a.nextRow();
    b.nextRow();
  }
}

TEST(Gray16Ditherer, ExactLevelsReproduceWithoutError) {
  Ditherer ditherer(32);
  ASSERT_TRUE(ditherer.isValid());
  for (int y = 0; y < 8; ++y) {
    for (uint16_t x = 0; x < 32; ++x) {
      const uint8_t level = static_cast<uint8_t>((x * 7 + y * 3) % LEVELS);
      EXPECT_EQ(ditherer.processPixel(static_cast<uint8_t>(level * 17), x), level) << "x=" << x << " y=" << y;
    }
    ditherer.nextRow();
  }
}

TEST(Gray16Ditherer, ExtremesSaturate) {
  Ditherer ditherer(16);
  for (int y = 0; y < 4; ++y) {
    for (uint16_t x = 0; x < 16; ++x) {
      EXPECT_EQ(ditherer.processPixel(x < 8 ? 0 : 255, x), x < 8 ? 0 : 15);
    }
    ditherer.nextRow();
  }
}

TEST(Gray16Ditherer, ResetDropsCarriedError) {
  Ditherer ditherer(8);
  ASSERT_TRUE(ditherer.isValid());
  const uint8_t fresh = ditherer.processPixel(128, 0);
  for (uint16_t x = 1; x < 8; ++x) ditherer.processPixel(128, x);
  ditherer.nextRow();
  ditherer.reset();
  EXPECT_EQ(ditherer.processPixel(128, 0), fresh);
}

TEST(Gray16Ditherer, ZeroWidthIsInvalidButStillQuantizes) {
  Ditherer ditherer(0);
  EXPECT_FALSE(ditherer.isValid());
  EXPECT_EQ(ditherer.processPixel(255, 0), 15);
  EXPECT_EQ(ditherer.processPixel(0, 0), 0);
  EXPECT_EQ(ditherer.processPixel(8, 0), 0);
  EXPECT_EQ(ditherer.processPixel(9, 0), 1);
  ditherer.nextRow();
  ditherer.reset();
}

// ---------------------------------------------------------------------------
// Writer / Reader round trip

namespace {

void expectExactRoundTrip(uint8_t orientation) {
  SCOPED_TRACE(int{orientation});
  const PageSpec spec = fullPageSpec(orientation, 32, 16, 8);
  ASSERT_TRUE(spec.valid());
  VecSink sink;
  // Exact multiples of 17 dither with zero error, so the planes are predictable.
  ASSERT_TRUE(writePage(spec, [](uint16_t x, uint16_t y) { return static_cast<uint8_t>(((x + y) % 16) * 17); }, &sink));
  EXPECT_EQ(sink.bytes.size(), fileSize(spec));
  EXPECT_EQ(sink.bytes.size(), 288u);

  PageSpec readSpec;
  std::vector<std::vector<uint8_t>> planes;
  ASSERT_TRUE(readPlanes(sink.bytes, &readSpec, &planes));
  EXPECT_EQ(readSpec.orientation, orientation);
  EXPECT_EQ(readSpec.pageWidth, spec.pageWidth);
  EXPECT_EQ(readSpec.pageHeight, spec.pageHeight);
  for (int y = 0; y < spec.pageHeight; ++y) {
    for (int x = 0; x < spec.pageWidth; ++x) {
      ASSERT_EQ(brightnessAt(planes, readSpec, x, y), (x + y) % 16) << "x=" << x << " y=" << y;
    }
  }
}

}  // namespace

TEST(Gray16RoundTrip, Portrait) { expectExactRoundTrip(Portrait); }
TEST(Gray16RoundTrip, LandscapeClockwise) { expectExactRoundTrip(LandscapeClockwise); }
TEST(Gray16RoundTrip, PortraitInverted) { expectExactRoundTrip(PortraitInverted); }
TEST(Gray16RoundTrip, LandscapeCounterClockwise) { expectExactRoundTrip(LandscapeCounterClockwise); }

TEST(Gray16RoundTrip, PartialLastBandsSurviveTheRoundTrip) {
  const PageSpec specs[] = {
      fullPageSpec(Portrait, 40, 16, 16),
      fullPageSpec(PortraitInverted, 40, 16, 16),
      fullPageSpec(LandscapeClockwise, 32, 24, 16),
      fullPageSpec(LandscapeCounterClockwise, 32, 24, 16),
  };
  for (const PageSpec& spec : specs) {
    SCOPED_TRACE(int{spec.orientation});
    VecSink sink;
    ASSERT_TRUE(
        writePage(spec, [](uint16_t x, uint16_t y) { return static_cast<uint8_t>(((x * 3 + y) % 16) * 17); }, &sink));
    EXPECT_EQ(sink.bytes.size(), fileSize(spec));
    PageSpec readSpec;
    std::vector<std::vector<uint8_t>> planes;
    ASSERT_TRUE(readPlanes(sink.bytes, &readSpec, &planes));
    for (int y = 0; y < spec.pageHeight; ++y) {
      for (int x = 0; x < spec.pageWidth; ++x) {
        ASSERT_EQ(brightnessAt(planes, readSpec, x, y), (x * 3 + y) % 16) << "x=" << x << " y=" << y;
      }
    }
  }
}

TEST(Gray16RoundTrip, WhiteAndBlackPagesFillEveryByte) {
  for (uint8_t orientation : ALL_ORIENTATIONS) {
    SCOPED_TRACE(int{orientation});
    const PageSpec spec = fullPageSpec(orientation, 32, 16, 8);
    VecSink white;
    ASSERT_TRUE(writePage(spec, [](uint16_t, uint16_t) { return static_cast<uint8_t>(255); }, &white));
    VecSink black;
    ASSERT_TRUE(writePage(spec, [](uint16_t, uint16_t) { return static_cast<uint8_t>(0); }, &black));
    for (size_t i = HEADER_SIZE; i < white.bytes.size(); ++i) ASSERT_EQ(white.bytes[i], 0xFF);
    for (size_t i = HEADER_SIZE; i < black.bytes.size(); ++i) ASSERT_EQ(black.bytes[i], 0x00);

    PageSpec readSpec;
    std::vector<std::vector<uint8_t>> planes;
    ASSERT_TRUE(readPlanes(white.bytes, &readSpec, &planes));
    for (const auto& plane : planes) {
      for (uint8_t byte : plane) ASSERT_EQ(byte, 0xFF);  // no 0xA5 fill left behind
    }
    ASSERT_TRUE(readPlanes(black.bytes, &readSpec, &planes));
    for (const auto& plane : planes) {
      for (uint8_t byte : plane) ASSERT_EQ(byte, 0x00);
    }
  }
}

TEST(Gray16RoundTrip, DitheredPagesAreDeterministic) {
  const PageSpec spec = fullPageSpec(Portrait, 32, 16, 8);
  auto gradient = [](uint16_t x, uint16_t y) { return static_cast<uint8_t>(x * 13 + y * 5); };
  VecSink first;
  VecSink second;
  ASSERT_TRUE(writePage(spec, gradient, &first));
  ASSERT_TRUE(writePage(spec, gradient, &second));
  EXPECT_EQ(first.bytes, second.bytes);
}

TEST(Gray16RoundTrip, WriterCanBeReusedAfterFinish) {
  const PageSpec spec = fullPageSpec(LandscapeCounterClockwise, 32, 16, 8);
  Writer writer(spec);
  ASSERT_TRUE(writer.isValid());
  std::vector<uint8_t> row(spec.imageWidth);
  VecSink first;
  VecSink second;
  for (VecSink* sink : {&first, &second}) {
    ASSERT_TRUE(writer.begin(writeToVec, sink));
    for (uint16_t y = 0; y < spec.imageHeight; ++y) {
      for (uint16_t x = 0; x < spec.imageWidth; ++x) row[x] = static_cast<uint8_t>(x * 9 + y * 7);
      ASSERT_TRUE(writer.addImageRow(row.data()));
    }
    ASSERT_TRUE(writer.finish());
  }
  EXPECT_EQ(first.bytes.size(), fileSize(spec));
  EXPECT_EQ(first.bytes, second.bytes);
}

// ---------------------------------------------------------------------------
// Letterbox

TEST(Gray16Letterbox, PixelsOutsideTheImageAreWhite) {
  for (uint8_t orientation : ALL_ORIENTATIONS) {
    SCOPED_TRACE(int{orientation});
    PageSpec spec = fullPageSpec(orientation, 32, 16, 8);
    // Deliberately unaligned placement; fits both the 16x32 and 32x16 pages.
    spec.imageX = 3;
    spec.imageY = 5;
    spec.imageWidth = 7;
    spec.imageHeight = 6;
    ASSERT_TRUE(spec.valid());
    VecSink sink;
    ASSERT_TRUE(writePage(spec, [](uint16_t, uint16_t) { return static_cast<uint8_t>(128); }, &sink));
    EXPECT_EQ(sink.bytes.size(), fileSize(spec));

    PageSpec readSpec;
    std::vector<std::vector<uint8_t>> planes;
    ASSERT_TRUE(readPlanes(sink.bytes, &readSpec, &planes));
    EXPECT_EQ(readSpec.imageX, 3);
    EXPECT_EQ(readSpec.imageHeight, 6);
    for (int y = 0; y < spec.pageHeight; ++y) {
      for (int x = 0; x < spec.pageWidth; ++x) {
        const uint8_t g = brightnessAt(planes, readSpec, x, y);
        const bool inside = x >= spec.imageX && x < spec.imageX + spec.imageWidth && y >= spec.imageY &&
                            y < spec.imageY + spec.imageHeight;
        if (inside) {
          ASSERT_TRUE(g == 7 || g == 8) << "x=" << x << " y=" << y << " g=" << int{g};
        } else {
          ASSERT_EQ(g, 15) << "x=" << x << " y=" << y;
        }
      }
    }
  }
}

TEST(Gray16Letterbox, FitImagePlacementIsCentredAndPadded) {
  PageSpec spec = fullPageSpec(Portrait, 32, 16, 8);  // page 16x32
  ASSERT_TRUE(fitImage(8, 8, &spec));
  EXPECT_EQ(spec.imageWidth, 16);
  EXPECT_EQ(spec.imageHeight, 16);
  EXPECT_EQ(spec.imageX, 0);
  EXPECT_EQ(spec.imageY, 8);
  VecSink sink;
  ASSERT_TRUE(writePage(spec, [](uint16_t, uint16_t) { return static_cast<uint8_t>(0); }, &sink));
  PageSpec readSpec;
  std::vector<std::vector<uint8_t>> planes;
  ASSERT_TRUE(readPlanes(sink.bytes, &readSpec, &planes));
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 16; ++x) {
      const uint8_t expected = (y >= 8 && y < 24) ? 0 : 15;
      ASSERT_EQ(brightnessAt(planes, readSpec, x, y), expected) << "x=" << x << " y=" << y;
    }
  }
}

TEST(Gray16Letterbox, EmptyImageProducesAWhitePage) {
  PageSpec spec = fullPageSpec(LandscapeClockwise, 32, 16, 8);
  spec.imageWidth = 0;
  spec.imageHeight = 0;
  Writer writer(spec);
  ASSERT_TRUE(writer.isValid());
  VecSink sink;
  ASSERT_TRUE(writer.begin(writeToVec, &sink));
  EXPECT_FALSE(writer.addImageRow(sink.bytes.data()));  // no image rows expected
  ASSERT_TRUE(writer.finish());
  ASSERT_EQ(sink.bytes.size(), fileSize(spec));
  for (size_t i = HEADER_SIZE; i < sink.bytes.size(); ++i) ASSERT_EQ(sink.bytes[i], 0xFF);
}

// ---------------------------------------------------------------------------
// Error paths

TEST(Gray16Writer, RejectsInvalidSpec) {
  PageSpec spec = fullPageSpec(Portrait, 32, 16, 12);  // bandRows not a multiple of 8
  Writer writer(spec);
  EXPECT_FALSE(writer.isValid());
  VecSink sink;
  EXPECT_FALSE(writer.begin(writeToVec, &sink));
  EXPECT_TRUE(sink.bytes.empty());
}

TEST(Gray16Writer, RowsBeforeBeginAreRejected) {
  const PageSpec spec = fullPageSpec(Portrait, 32, 16, 8);
  Writer writer(spec);
  ASSERT_TRUE(writer.isValid());
  const uint8_t row[16] = {};
  EXPECT_FALSE(writer.addImageRow(row));
  EXPECT_FALSE(writer.finish());
  VecSink sink;
  EXPECT_FALSE(writer.begin(nullptr, &sink));
  ASSERT_TRUE(writer.begin(writeToVec, &sink));
  EXPECT_FALSE(writer.begin(writeToVec, &sink));  // already begun
  EXPECT_FALSE(writer.addImageRow(nullptr));
  EXPECT_EQ(sink.bytes.size(), HEADER_SIZE);
}

TEST(Gray16Writer, TooManyRowsAreRejected) {
  const PageSpec spec = fullPageSpec(Portrait, 32, 16, 8);
  Writer writer(spec);
  VecSink sink;
  ASSERT_TRUE(writer.begin(writeToVec, &sink));
  const uint8_t row[16] = {};
  for (uint16_t y = 0; y < spec.imageHeight; ++y) ASSERT_TRUE(writer.addImageRow(row));
  EXPECT_FALSE(writer.addImageRow(row));
  EXPECT_TRUE(writer.finish());
  EXPECT_EQ(sink.bytes.size(), fileSize(spec));
  EXPECT_FALSE(writer.finish());  // already finished
}

TEST(Gray16Writer, FinishWithMissingRowsFailsButPadsTheFile) {
  const PageSpec spec = fullPageSpec(PortraitInverted, 32, 16, 8);
  Writer writer(spec);
  VecSink sink;
  ASSERT_TRUE(writer.begin(writeToVec, &sink));
  const uint8_t row[16] = {};
  for (int y = 0; y < 10; ++y) ASSERT_TRUE(writer.addImageRow(row));
  EXPECT_FALSE(writer.finish());
  EXPECT_EQ(sink.bytes.size(), fileSize(spec));
  EXPECT_FALSE(writer.addImageRow(row));
}

TEST(Gray16Writer, WriteFailuresAreSticky) {
  const PageSpec spec = fullPageSpec(LandscapeCounterClockwise, 32, 16, 8);
  Writer writer(spec);
  VecSink sink;
  ASSERT_TRUE(writer.begin(writeToVec, &sink));
  const uint8_t row[32] = {};
  for (int y = 0; y < 7; ++y) ASSERT_TRUE(writer.addImageRow(row));
  sink.fail = true;
  EXPECT_FALSE(writer.addImageRow(row));  // row 8 completes the first band
  sink.fail = false;
  EXPECT_FALSE(writer.addImageRow(row));
  EXPECT_FALSE(writer.finish());
  EXPECT_EQ(sink.bytes.size(), HEADER_SIZE);

  // A failed writer may begin() again; a failed header write is reported too.
  VecSink headerFail;
  headerFail.fail = true;
  EXPECT_FALSE(writer.begin(writeToVec, &headerFail));
  EXPECT_TRUE(headerFail.bytes.empty());
  VecSink retry;
  ASSERT_TRUE(writer.begin(writeToVec, &retry));
  for (uint16_t y = 0; y < spec.imageHeight; ++y) ASSERT_TRUE(writer.addImageRow(row));
  EXPECT_TRUE(writer.finish());
  EXPECT_EQ(retry.bytes.size(), fileSize(spec));
}

TEST(Gray16Reader, OpenRejectsBadHeadersAndShortFiles) {
  Reader reader;
  std::vector<uint8_t> empty;
  EXPECT_FALSE(reader.open(readFromVec, &empty));
  EXPECT_FALSE(reader.isOpen());
  EXPECT_FALSE(reader.open(nullptr, &empty));

  std::vector<uint8_t> header(HEADER_SIZE);
  encodeHeader(fullPageSpec(Portrait, 32, 16, 8), header.data());
  header[1] = 'x';
  EXPECT_FALSE(reader.open(readFromVec, &header));
  EXPECT_FALSE(reader.isOpen());
  EXPECT_EQ(reader.tileOffset(0, 0), 0u);

  header[1] = '1';
  EXPECT_TRUE(reader.open(readFromVec, &header));
  EXPECT_TRUE(reader.isOpen());
}

TEST(Gray16Reader, LoadPlaneValidatesArguments) {
  const PageSpec spec = fullPageSpec(Portrait, 32, 16, 8);
  VecSink sink;
  ASSERT_TRUE(writePage(spec, [](uint16_t, uint16_t) { return static_cast<uint8_t>(200); }, &sink));
  Reader reader;
  uint8_t fb[64];
  uint8_t scratch[16];
  EXPECT_FALSE(reader.loadPlane(0, fb, scratch, sizeof(scratch)));  // not open
  ASSERT_TRUE(reader.open(readFromVec, &sink.bytes));
  EXPECT_EQ(reader.spec().maxTileBytes(), sizeof(scratch));
  EXPECT_TRUE(reader.loadPlane(0, fb, scratch, sizeof(scratch)));
  EXPECT_FALSE(reader.loadPlane(0, fb, scratch, sizeof(scratch) - 1));
  EXPECT_FALSE(reader.loadPlane(PLANE_COUNT, fb, scratch, sizeof(scratch)));
  EXPECT_FALSE(reader.loadPlane(0, nullptr, scratch, sizeof(scratch)));
  EXPECT_FALSE(reader.loadPlane(0, fb, nullptr, sizeof(scratch)));

  // A truncated body fails the read of the missing tile.
  std::vector<uint8_t> truncated(sink.bytes.begin(), sink.bytes.end() - 1);
  Reader truncatedReader;
  ASSERT_TRUE(truncatedReader.open(readFromVec, &truncated));
  EXPECT_TRUE(truncatedReader.loadPlane(0, fb, scratch, sizeof(scratch)));
  EXPECT_FALSE(truncatedReader.loadPlane(3, fb, scratch, sizeof(scratch)));
}

TEST(Gray16Reader, TileOffsetsFollowTheBandLayout) {
  // 32x16 portrait, bandRows 8: 16-byte tiles, 64 bytes per band.
  std::vector<uint8_t> header(HEADER_SIZE);
  encodeHeader(fullPageSpec(Portrait, 32, 16, 8), header.data());
  Reader reader;
  ASSERT_TRUE(reader.open(readFromVec, &header));
  for (uint16_t band = 0; band < 4; ++band) {
    for (uint8_t k = 0; k < PLANE_COUNT; ++k) {
      EXPECT_EQ(reader.tileOffset(band, k), 32u + band * 64u + k * 16u);
    }
  }
  EXPECT_EQ(reader.tileOffset(4, 0), 0u);
  EXPECT_EQ(reader.tileOffset(0, PLANE_COUNT), 0u);

  // 40x16 portrait, bandRows 16: 32-byte tiles, the last band 16 bytes.
  encodeHeader(fullPageSpec(Portrait, 40, 16, 16), header.data());
  ASSERT_TRUE(reader.open(readFromVec, &header));
  EXPECT_EQ(reader.tileOffset(1, 0), 32u + 128u);
  EXPECT_EQ(reader.tileOffset(2, 0), 32u + 256u);
  EXPECT_EQ(reader.tileOffset(2, 1), 32u + 256u + 16u);
  EXPECT_EQ(reader.tileOffset(2, 3), 32u + 256u + 48u);
  EXPECT_EQ(reader.tileOffset(3, 0), 0u);
  EXPECT_EQ(reader.tileOffset(2, 3) + 16u, fileSize(reader.spec()));
}

TEST(Gray16Reader, TileOffsetsAddressTheWrittenTiles) {
  // Black image rows 0..7 only: in portrait that is band 0, so its tiles are
  // all zero and every later tile is all white.
  PageSpec spec = fullPageSpec(Portrait, 32, 16, 8);
  spec.imageY = 0;
  spec.imageHeight = 8;
  VecSink sink;
  ASSERT_TRUE(writePage(spec, [](uint16_t, uint16_t) { return static_cast<uint8_t>(0); }, &sink));
  Reader reader;
  ASSERT_TRUE(reader.open(readFromVec, &sink.bytes));
  for (uint16_t band = 0; band < 4; ++band) {
    for (uint8_t k = 0; k < PLANE_COUNT; ++k) {
      const uint32_t offset = reader.tileOffset(band, k);
      const uint8_t expected = band == 0 ? 0x00 : 0xFF;
      for (uint32_t i = 0; i < 16; ++i)
        ASSERT_EQ(sink.bytes[offset + i], expected) << "band " << band << " k " << int{k};
    }
  }
}

// ---------------------------------------------------------------------------
// Allocation failure

namespace {
// Index of the next nothrow array allocation to fail; -1 disables the hook.
thread_local int arrayAllocationToFail = -1;
}  // namespace

// Fail one nothrow array allocation without changing production allocator
// APIs (the same hook test/absolute_grayscale uses).
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (arrayAllocationToFail >= 0 && arrayAllocationToFail-- == 0) return nullptr;
  return ::operator new[](size);
}

TEST(Gray16Allocation, DithererReportsRowAllocationFailure) {
  arrayAllocationToFail = 0;  // errCur
  Ditherer firstFails(64);
  arrayAllocationToFail = 1;  // errNext
  Ditherer secondFails(64);
  arrayAllocationToFail = -1;
  EXPECT_FALSE(firstFails.isValid());
  EXPECT_FALSE(secondFails.isValid());
  EXPECT_EQ(secondFails.processPixel(255, 0), 15);  // still quantizes safely
}

TEST(Gray16Allocation, WriterReportsBufferAllocationFailure) {
  const PageSpec spec = fullPageSpec(Portrait, 32, 16, 8);
  arrayAllocationToFail = 0;  // band tiles
  Writer noTiles(spec);
  arrayAllocationToFail = 1;  // the ditherer's first error row
  Writer noDitherer(spec);
  arrayAllocationToFail = -1;
  EXPECT_FALSE(noTiles.isValid());
  EXPECT_FALSE(noDitherer.isValid());
  VecSink sink;
  EXPECT_FALSE(noTiles.begin(writeToVec, &sink));
  EXPECT_FALSE(noDitherer.begin(writeToVec, &sink));
  EXPECT_TRUE(sink.bytes.empty());
  Writer healthy(spec);
  EXPECT_TRUE(healthy.isValid());
}
