#include <HalStorage.h>
#include <gtest/gtest.h>
#include <util/Gray16Lut.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using gray16lut::FineConfig;
using gray16lut::LUT_SIZE;
using gray16lut::Source;

namespace {

using Lut = std::array<uint8_t, LUT_SIZE>;

struct Parsed {
  bool ok = false;
  FineConfig cfg;
  std::string error;
};

Parsed parse(const std::string& text) {
  Parsed result;
  char error[64];
  result.ok = gray16lut::parseConfig(text.data(), text.size(), &result.cfg, error, sizeof(error));
  result.error = error;
  return result;
}

Lut build(const FineConfig& cfg) {
  Lut lut{};
  gray16lut::buildLut(cfg, lut.data());
  return lut;
}

// The LUT the default FineConfig must produce: darken 2 4 7 -> groups of
// 2, 2, 3 frames; v = 0 (row 0) drives all three, v = 1 (row 2) the first two,
// v = 2 (row 1) the first; FR 0x22 everywhere; X4 factory voltage tail.
Lut expectedDefaultLut() {
  Lut lut{};
  lut[0] = lut[1] = lut[2] = 0x40;  // row 0, phase A = VSH1 in groups 0..2
  lut[10] = 0x40;                   // row 1, group 0
  lut[20] = lut[21] = 0x40;         // row 2, groups 0..1
  lut[50] = 2;                      // group 0 TPA
  lut[55] = 2;                      // group 1 TPA
  lut[60] = 3;                      // group 2 TPA
  for (size_t i = 100; i < 105; i++) lut[i] = 0x22;
  const uint8_t tail[5] = {0x17, 0x41, 0xA8, 0x32, 0x30};
  memcpy(&lut[105], tail, sizeof(tail));
  return lut;
}

std::vector<uint8_t> bytesOf(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

constexpr const char* FULL_CONFIG =
    "# extra frames after the coarse pass\n"
    "darken = 1 3 6\n"
    "fr = 5          # frame-rate code\n"
    "vgh = 0x10\n"
    "VSH1 = 65\n"
    "vsh2=0xa0\n"
    "Vsl = 50\n"
    "vcom = 0x2F\n";

}  // namespace

TEST(Gray16LutParse, EmptyTextKeepsDefaults) {
  const Parsed p = parse("");
  ASSERT_TRUE(p.ok) << p.error;
  EXPECT_EQ(p.cfg.darken[0], 2);
  EXPECT_EQ(p.cfg.darken[1], 4);
  EXPECT_EQ(p.cfg.darken[2], 7);
  EXPECT_EQ(p.cfg.frameRate, 0x2);
  const uint8_t tail[5] = {0x17, 0x41, 0xA8, 0x32, 0x30};
  EXPECT_EQ(memcmp(p.cfg.voltages, tail, sizeof(tail)), 0);
  EXPECT_TRUE(p.error.empty());
}

TEST(Gray16LutParse, ReadsEveryKeyWithDecimalHexAndAnyCase) {
  const Parsed p = parse(FULL_CONFIG);
  ASSERT_TRUE(p.ok) << p.error;
  EXPECT_EQ(p.cfg.darken[0], 1);
  EXPECT_EQ(p.cfg.darken[1], 3);
  EXPECT_EQ(p.cfg.darken[2], 6);
  EXPECT_EQ(p.cfg.frameRate, 5);
  EXPECT_EQ(p.cfg.voltages[0], 0x10);
  EXPECT_EQ(p.cfg.voltages[1], 65);
  EXPECT_EQ(p.cfg.voltages[2], 0xA0);
  EXPECT_EQ(p.cfg.voltages[3], 50);
  EXPECT_EQ(p.cfg.voltages[4], 0x2F);
}

TEST(Gray16LutParse, SkipsCommentsBlankLinesAndCrLf) {
  const Parsed p = parse("\r\n   # only a comment\r\n\r\n\tdarken =\t-3   0  +9 \r\n#fr = 9\r\n  fr = 0xF\r\n");
  ASSERT_TRUE(p.ok) << p.error;
  EXPECT_EQ(p.cfg.darken[0], -3);
  EXPECT_EQ(p.cfg.darken[1], 0);
  EXPECT_EQ(p.cfg.darken[2], 9);
  EXPECT_EQ(p.cfg.frameRate, 15);
}

TEST(Gray16LutParse, LastLineWithoutNewlineAndExtremeValues) {
  const Parsed p = parse("darken = -31 31 0\nvcom = 0x78\nvgh = 3");
  ASSERT_TRUE(p.ok) << p.error;
  EXPECT_EQ(p.cfg.darken[0], -31);
  EXPECT_EQ(p.cfg.darken[1], 31);
  EXPECT_EQ(p.cfg.voltages[4], 0x78);
  EXPECT_EQ(p.cfg.voltages[0], 3);
}

TEST(Gray16LutParse, RejectsUnknownKey) {
  const Parsed p = parse("fr = 2\nfrr = 2\n");
  EXPECT_FALSE(p.ok);
  EXPECT_NE(p.error.find("line 2"), std::string::npos) << p.error;
  EXPECT_NE(p.error.find("unknown key"), std::string::npos) << p.error;
}

TEST(Gray16LutParse, RejectsLinesWithoutKeyValue) {
  EXPECT_FALSE(parse("darken\n").ok);
  EXPECT_FALSE(parse("darken 2 4 7\n").ok);
  EXPECT_FALSE(parse("= 2\n").ok);
  EXPECT_FALSE(parse("fr =\n").ok);
  EXPECT_FALSE(parse("fr = # 2\n").ok);
}

TEST(Gray16LutParse, RejectsWrongDarkenCount) {
  EXPECT_FALSE(parse("darken = 2 4\n").ok);
  EXPECT_FALSE(parse("darken = 2 4 7 9\n").ok);
  EXPECT_FALSE(parse("darken = 2, 4, 7\n").ok);
}

TEST(Gray16LutParse, RejectsOutOfRangeValues) {
  EXPECT_FALSE(parse("darken = 32 0 0\n").ok);
  EXPECT_FALSE(parse("darken = 0 -32 0\n").ok);
  EXPECT_FALSE(parse("darken = 128 0 0\n").ok);
  EXPECT_FALSE(parse("fr = 16\n").ok);
  EXPECT_FALSE(parse("fr = -1\n").ok);
  EXPECT_FALSE(parse("vcom = 256\n").ok);
  EXPECT_FALSE(parse("vsl = -1\n").ok);
  EXPECT_FALSE(parse("vsh1 = 99999999999\n").ok);
  const Parsed p = parse("vgh = 0x100\n");
  EXPECT_FALSE(p.ok);
  EXPECT_NE(p.error.find("out of range"), std::string::npos) << p.error;
}

TEST(Gray16LutParse, RejectsVoltageCodesOutsideTheControllerRanges) {
  EXPECT_FALSE(parse("vgh = 0x02\n").ok);
  EXPECT_FALSE(parse("vgh = 0x18\n").ok);
  EXPECT_FALSE(parse("vsh1 = 0x22\n").ok);
  EXPECT_FALSE(parse("vsh1 = 0x51\n").ok);
  EXPECT_FALSE(parse("vsh2 = 0x8D\n").ok);
  EXPECT_FALSE(parse("vsh2 = 0xCF\n").ok);
  EXPECT_FALSE(parse("vsl = 0x09\n").ok);
  EXPECT_FALSE(parse("vsl = 0x3B\n").ok);
  EXPECT_FALSE(parse("vcom = 0x07\n").ok);
  const Parsed p = parse("vcom = 0x79\n");
  EXPECT_FALSE(p.ok);
  EXPECT_NE(p.error.find("SSD1677"), std::string::npos) << p.error;
  // Both documented VSH bands are accepted.
  EXPECT_TRUE(parse("vsh1 = 0x23\nvsh2 = 0xCE\n").ok);
}

TEST(Gray16LutBuild, ValidateLutAcceptsBuilderOutput) {
  char error[64] = "x";
  EXPECT_TRUE(gray16lut::validateLut(build(FineConfig{}).data(), error, sizeof(error)));
  EXPECT_EQ(error[0], '\0');
  FineConfig longest;
  longest.darken[0] = 31;
  longest.darken[1] = -31;
  longest.darken[2] = 31;
  EXPECT_TRUE(gray16lut::validateLut(build(longest).data(), error, sizeof(error)));
  EXPECT_TRUE(gray16lut::validateLut(build(parse(FULL_CONFIG).cfg).data(), nullptr, 0));
}

TEST(Gray16LutBuild, ValidateLutRejectsBadVoltageAndLongWaveform) {
  char error[64];
  Lut lut = expectedDefaultLut();
  lut[109] = 0x79;  // VCOM below -3.0 V
  EXPECT_FALSE(gray16lut::validateLut(lut.data(), error, sizeof(error)));
  EXPECT_NE(std::string(error).find("voltage byte 4"), std::string::npos) << error;
  lut = expectedDefaultLut();
  lut[106] = 0x60;  // VSH1 in the reserved gap
  EXPECT_FALSE(gray16lut::validateLut(lut.data(), nullptr, 0));
  lut = expectedDefaultLut();
  lut[50] = 200;  // group 0: 200 frames
  lut[54] = 1;    // repeated once -> 400 + 5 frames
  EXPECT_FALSE(gray16lut::validateLut(lut.data(), error, sizeof(error)));
  EXPECT_NE(std::string(error).find("frames"), std::string::npos) << error;
  lut[54] = 0;  // 200 + 2 + 3 = 205 frames: allowed
  EXPECT_TRUE(gray16lut::validateLut(lut.data(), error, sizeof(error)));
  EXPECT_FALSE(gray16lut::validateLut(nullptr, nullptr, 0));
}

TEST(Gray16LutParse, RejectsMalformedNumbers) {
  EXPECT_FALSE(parse("fr = 0x\n").ok);
  EXPECT_FALSE(parse("fr = 1z\n").ok);
  EXPECT_FALSE(parse("fr = two\n").ok);
  EXPECT_FALSE(parse("vsl = 0x1G\n").ok);
  EXPECT_FALSE(parse("fr = 2 3\n").ok);
  EXPECT_FALSE(parse("fr = -\n").ok);
}

TEST(Gray16LutParse, ToleratesMissingOrTinyErrorBuffer) {
  FineConfig cfg;
  const std::string bad = "nope = 1\n";
  EXPECT_FALSE(gray16lut::parseConfig(bad.data(), bad.size(), &cfg, nullptr, 0));
  char one[1] = {'x'};
  EXPECT_FALSE(gray16lut::parseConfig(bad.data(), bad.size(), &cfg, one, sizeof(one)));
  EXPECT_EQ(one[0], '\0');
  EXPECT_FALSE(gray16lut::parseConfig(bad.data(), bad.size(), nullptr, nullptr, 0));
}

TEST(Gray16LutBuild, DefaultConfigMatchesLayout) {
  const Lut lut = build(FineConfig{});
  EXPECT_EQ(lut, expectedDefaultLut());
  // Spot-check the layout boundaries the controller cares about.
  for (size_t i = 30; i < 50; i++) EXPECT_EQ(lut[i], 0) << "row 3 / VCOM byte " << i;
  for (size_t g = 0; g < 10; g++) {
    for (size_t b = 1; b < 5; b++) EXPECT_EQ(lut[50 + g * 5 + b], 0) << "TPB..RP of group " << g;
  }
  EXPECT_EQ(lut[110], 0);
  EXPECT_EQ(lut[111], 0);
}

TEST(Gray16LutBuild, NegativeDarkenDrivesVsl) {
  FineConfig cfg;
  cfg.darken[0] = -2;  // v = 2 lightens for two frames
  const Lut lut = build(cfg);
  Lut expected = expectedDefaultLut();
  expected[10] = 0x80;  // row 1, phase A = VSL
  EXPECT_EQ(lut, expected);
}

TEST(Gray16LutBuild, ZeroDarkenHasNoGroups) {
  FineConfig cfg;
  cfg.darken[0] = cfg.darken[1] = cfg.darken[2] = 0;
  cfg.frameRate = 0x9;
  const Lut lut = build(cfg);
  for (size_t i = 0; i < 100; i++) EXPECT_EQ(lut[i], 0) << "byte " << i;
  for (size_t i = 100; i < 105; i++) EXPECT_EQ(lut[i], 0x99);
  EXPECT_EQ(lut[105], 0x17);
  EXPECT_EQ(lut[109], 0x30);
}

TEST(Gray16LutBuild, EqualAndUnsortedMagnitudesShareGroups) {
  FineConfig cfg;
  cfg.darken[0] = 7;
  cfg.darken[1] = 4;
  cfg.darken[2] = -4;
  const Lut lut = build(cfg);
  EXPECT_EQ(lut[50], 4);     // group 0: 4 frames
  EXPECT_EQ(lut[55], 3);     // group 1: 7 - 4 frames
  EXPECT_EQ(lut[60], 0);     // no third group
  EXPECT_EQ(lut[10], 0x40);  // v = 2 (7 frames): both groups
  EXPECT_EQ(lut[11], 0x40);
  EXPECT_EQ(lut[20], 0x40);  // v = 1 (4 frames): group 0 only
  EXPECT_EQ(lut[21], 0);
  EXPECT_EQ(lut[0], 0x80);  // v = 0 (-4 frames): VSL in group 0 only
  EXPECT_EQ(lut[1], 0);
}

TEST(Gray16LutBuild, OverwritesStaleOutput) {
  Lut dirty;
  dirty.fill(0xFF);
  gray16lut::buildLut(FineConfig{}, dirty.data());
  EXPECT_EQ(dirty, expectedDefaultLut());
}

TEST(Gray16LutBuild, ConfigVoltagesAndFrameRateLandInTheTail) {
  const Parsed p = parse(FULL_CONFIG);
  ASSERT_TRUE(p.ok) << p.error;
  const Lut lut = build(p.cfg);
  EXPECT_EQ(lut[50], 1);
  EXPECT_EQ(lut[55], 2);
  EXPECT_EQ(lut[60], 3);
  for (size_t i = 100; i < 105; i++) EXPECT_EQ(lut[i], 0x55);
  EXPECT_EQ(lut[105], 0x10);
  EXPECT_EQ(lut[106], 65);
  EXPECT_EQ(lut[107], 0xA0);
  EXPECT_EQ(lut[108], 50);
  EXPECT_EQ(lut[109], 0x2F);
}

class Gray16LutStorageTest : public ::testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }

  // A valid raw LUT with recognisable VS bytes: the builder's timing and the
  // factory voltage tail, VS rows and frame-rate bytes patterned.
  static std::vector<uint8_t> rawPattern(size_t size) {
    Lut base = expectedDefaultLut();
    for (size_t i = 0; i < 50; i++) base[i] = static_cast<uint8_t>(0xA0 + i);
    for (size_t i = 100; i < 105; i++) base[i] = 0x33;
    std::vector<uint8_t> raw(size);
    for (size_t i = 0; i < size; i++) raw[i] = i < LUT_SIZE ? base[i] : static_cast<uint8_t>(i);
    return raw;
  }

  Lut out{};
};

TEST_F(Gray16LutStorageTest, NothingOnTheCardLeavesOutputUntouched) {
  out.fill(0xEE);
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::None);
  for (uint8_t b : out) EXPECT_EQ(b, 0xEE);
}

TEST_F(Gray16LutStorageTest, RawFileWinsOverConfig) {
  const std::vector<uint8_t> raw = rawPattern(LUT_SIZE);
  Storage.setFile(gray16lut::RAW_LUT_PATH, raw);
  Storage.setFile(gray16lut::CONFIG_PATH, bytesOf(FULL_CONFIG));
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::RawFile);
  EXPECT_TRUE(std::equal(raw.begin(), raw.end(), out.begin()));
}

TEST_F(Gray16LutStorageTest, WrongSizeRawFileFallsBackToConfig) {
  Storage.setFile(gray16lut::RAW_LUT_PATH, rawPattern(LUT_SIZE - 1));
  Storage.setFile(gray16lut::CONFIG_PATH, bytesOf(FULL_CONFIG));
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::Config);
  EXPECT_EQ(out, build(parse(FULL_CONFIG).cfg));

  Storage.setFile(gray16lut::RAW_LUT_PATH, rawPattern(LUT_SIZE + 1));
  out.fill(0);
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::Config);
  EXPECT_EQ(out, build(parse(FULL_CONFIG).cfg));
}

TEST_F(Gray16LutStorageTest, UnsafeRawFileFallsBackToConfig) {
  std::vector<uint8_t> raw = rawPattern(LUT_SIZE);
  raw[109] = 0x79;  // VCOM out of range
  Storage.setFile(gray16lut::RAW_LUT_PATH, raw);
  Storage.setFile(gray16lut::CONFIG_PATH, bytesOf(FULL_CONFIG));
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::Config);
  EXPECT_EQ(out, build(parse(FULL_CONFIG).cfg));

  raw = rawPattern(LUT_SIZE);
  raw[50] = 255;  // 255 + 2 + 3 frames
  Storage.setFile(gray16lut::RAW_LUT_PATH, raw);
  Storage.remove(gray16lut::CONFIG_PATH);
  out.fill(0xEE);
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::None);
  for (uint8_t b : out) EXPECT_EQ(b, 0xEE);
}

TEST_F(Gray16LutStorageTest, WrongSizeRawFileAloneGivesNone) {
  Storage.setFile(gray16lut::RAW_LUT_PATH, rawPattern(LUT_SIZE - 1));
  out.fill(0xEE);
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::None);
  for (uint8_t b : out) EXPECT_EQ(b, 0xEE);
}

TEST_F(Gray16LutStorageTest, ConfigAloneBuildsTheLut) {
  Storage.setFile(gray16lut::CONFIG_PATH, bytesOf("darken = 2 4 7\n"));
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::Config);
  EXPECT_EQ(out, expectedDefaultLut());
}

TEST_F(Gray16LutStorageTest, InvalidOrOversizedConfigGivesNone) {
  out.fill(0xEE);
  Storage.setFile(gray16lut::CONFIG_PATH, bytesOf("darken = 2 4\n"));
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::None);
  Storage.setFile(gray16lut::CONFIG_PATH, bytesOf(std::string(2048, '#')));
  EXPECT_EQ(gray16lut::loadFromStorage(out.data()), Source::None);
  for (uint8_t b : out) EXPECT_EQ(b, 0xEE);
}
