#include "Gray16Lut.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace gray16lut {
namespace {

// Byte offsets of the SSD1677 LUT layout (Ssd1677Luts.h): five VS rows of 10
// bytes, ten TP/RP groups of 5 bytes, 5 frame-rate bytes, 5 voltages, 2 spare.
constexpr size_t VS_ROW_BYTES = 10;
constexpr size_t TP_OFFSET = 50;
constexpr size_t TP_GROUP_BYTES = 5;
constexpr size_t FR_OFFSET = 100;
constexpr size_t FR_BYTES = 5;
constexpr size_t VOLTAGE_OFFSET = 105;
constexpr uint8_t PHASE_A_VSH1 = 0x01 << 6;
constexpr uint8_t PHASE_A_VSL = 0x02 << 6;
// Controller row of each `darken` entry (public values v = 2, 1, 0): row index
// is (BW-RAM bit << 1) | RED-RAM bit and pass 1 writes LSB to BW, MSB to RED.
constexpr uint8_t ROW_OF_CLASS[3] = {1, 2, 0};

constexpr int MAX_DARKEN = 31;
constexpr unsigned MAX_WAVEFORM_FRAMES = 255;

// Documented SSD1677 register ranges (VGH 0x03, VSH1/VSH2 and VSL 0x04, VCOM
// 0x2C); other codes are reserved and the registers keep their value until
// the next OTP-sequence refresh, so a bad file must never reach the panel.
bool voltageCodeValid(size_t index, uint8_t code) {
  switch (index) {
    case 0:
      return code >= 0x03 && code <= 0x17;  // VGH 10..20 V
    case 1:
    case 2:
      return (code >= 0x23 && code <= 0x50) || (code >= 0x8E && code <= 0xCE);  // VSH 9..18 V or 2.4..8.8 V
    case 3:
      return code >= 0x0A && code <= 0x3A;  // VSL -5..-17 V
    default:
      return code >= 0x08 && code <= 0x78;  // VCOM -0.2..-3.0 V
  }
}

// A config is a few short lines; the cap bounds the stack buffer loadFromStorage
// uses at boot and in the lab (main loop task, no deep call chain below it).
constexpr size_t CONFIG_MAX_BYTES = 512;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

int digitValue(char c, int base) {
  int value = -1;
  if (c >= '0' && c <= '9') value = c - '0';
  if (base == 16 && lower(c) >= 'a' && lower(c) <= 'f') value = lower(c) - 'a' + 10;
  return value < base ? value : -1;
}

void skipSpaces(const char** p, const char* end) {
  while (*p < end && isSpace(**p)) (*p)++;
}

// One optionally signed decimal or 0x-hex integer at *p, advanced past it.
// Fails without digits, on a trailing non-space character, or beyond +-32767.
bool parseInt(const char** p, const char* end, int* out) {
  const char* s = *p;
  bool negative = false;
  if (s < end && (*s == '-' || *s == '+')) {
    negative = *s == '-';
    s++;
  }
  int base = 10;
  if (end - s >= 2 && s[0] == '0' && lower(s[1]) == 'x') {
    base = 16;
    s += 2;
  }
  long value = 0;
  int digits = 0;
  for (; s < end; s++, digits++) {
    const int d = digitValue(*s, base);
    if (d < 0) break;
    value = value * base + d;
    if (value > 32767) return false;
  }
  if (digits == 0 || (s < end && !isSpace(*s))) return false;
  *out = static_cast<int>(negative ? -value : value);
  *p = s;
  return true;
}

bool keyIs(const char* begin, const char* end, const char* name) {
  size_t i = 0;
  for (; begin + i < end && name[i] != '\0'; i++) {
    if (lower(begin[i]) != name[i]) return false;
  }
  return begin + i == end && name[i] == '\0';
}

// Exactly `count` integers in [lo, hi] and nothing else in [v, end).
bool readValues(const char* v, const char* end, int count, int lo, int hi, int* values, const char** reason) {
  for (int i = 0; i < count; i++) {
    skipSpaces(&v, end);
    if (!parseInt(&v, end, &values[i])) {
      *reason = count > 1 ? "expected three numbers" : "expected a number";
      return false;
    }
    if (values[i] < lo || values[i] > hi) {
      *reason = "value out of range";
      return false;
    }
  }
  skipSpaces(&v, end);
  if (v != end) {
    *reason = count > 1 ? "expected exactly three numbers" : "unexpected text after the value";
    return false;
  }
  return true;
}

bool applyKey(const char* key, const char* keyEnd, const char* v, const char* end, FineConfig* out,
              const char** reason) {
  int values[3];
  if (keyIs(key, keyEnd, "darken")) {
    if (!readValues(v, end, 3, -MAX_DARKEN, MAX_DARKEN, values, reason)) return false;
    for (int i = 0; i < 3; i++) out->darken[i] = static_cast<int8_t>(values[i]);
    return true;
  }
  if (keyIs(key, keyEnd, "fr")) {
    if (!readValues(v, end, 1, 0, 15, values, reason)) return false;
    out->frameRate = static_cast<uint8_t>(values[0]);
    return true;
  }
  static const char* const VOLTAGE_KEYS[5] = {"vgh", "vsh1", "vsh2", "vsl", "vcom"};
  for (int i = 0; i < 5; i++) {
    if (!keyIs(key, keyEnd, VOLTAGE_KEYS[i])) continue;
    if (!readValues(v, end, 1, 0, 255, values, reason)) return false;
    if (!voltageCodeValid(i, static_cast<uint8_t>(values[0]))) {
      *reason = "voltage code outside the SSD1677 range";
      return false;
    }
    out->voltages[i] = static_cast<uint8_t>(values[0]);
    return true;
  }
  *reason = "unknown key";
  return false;
}

void setError(char* error, size_t errorLen, int line, const char* reason) {
  if (error && errorLen) snprintf(error, errorLen, "line %d: %s", line, reason);
}

}  // namespace

bool parseConfig(const char* text, size_t len, FineConfig* out, char* error, size_t errorLen) {
  if (error && errorLen) error[0] = '\0';
  if (!out || (!text && len)) return false;
  const char* p = text;
  const char* const end = text + len;
  for (int line = 1; p < end; line++) {
    const char* lineEnd = p;
    while (lineEnd < end && *lineEnd != '\n') lineEnd++;
    const char* const next = lineEnd < end ? lineEnd + 1 : end;
    // Drop the comment and the surrounding whitespace.
    const char* hash = p;
    while (hash < lineEnd && *hash != '#') hash++;
    lineEnd = hash;
    skipSpaces(&p, lineEnd);
    while (lineEnd > p && isSpace(lineEnd[-1])) lineEnd--;
    if (p < lineEnd) {
      const char* keyEnd = p;
      while (keyEnd < lineEnd && *keyEnd != '=' && !isSpace(*keyEnd)) keyEnd++;
      const char* v = keyEnd;
      skipSpaces(&v, lineEnd);
      const char* reason = "expected key = value";
      if (v >= lineEnd || *v != '=') {
        setError(error, errorLen, line, reason);
        return false;
      }
      v++;
      if (!applyKey(p, keyEnd, v, lineEnd, out, &reason)) {
        setError(error, errorLen, line, reason);
        return false;
      }
    }
    p = next;
  }
  return true;
}

void buildLut(const FineConfig& cfg, uint8_t out[LUT_SIZE]) {
  memset(out, 0, LUT_SIZE);
  // Distinct nonzero magnitudes, ascending: each becomes a timing group whose
  // phase A lasts the step up from the previous magnitude.
  uint8_t levels[3];
  size_t levelCount = 0;
  for (size_t i = 0; i < 3; i++) {
    const uint8_t m = static_cast<uint8_t>(cfg.darken[i] < 0 ? -cfg.darken[i] : cfg.darken[i]);
    if (m == 0) continue;
    size_t pos = 0;
    while (pos < levelCount && levels[pos] < m) pos++;
    if (pos < levelCount && levels[pos] == m) continue;
    for (size_t j = levelCount; j > pos; j--) levels[j] = levels[j - 1];
    levels[pos] = m;
    levelCount++;
  }
  for (size_t g = 0; g < levelCount; g++) {
    out[TP_OFFSET + g * TP_GROUP_BYTES] = static_cast<uint8_t>(levels[g] - (g ? levels[g - 1] : 0));
  }
  // A class drives its polarity in every group up to its own magnitude.
  for (size_t i = 0; i < 3; i++) {
    if (cfg.darken[i] == 0) continue;
    const uint8_t m = static_cast<uint8_t>(cfg.darken[i] < 0 ? -cfg.darken[i] : cfg.darken[i]);
    const uint8_t phaseA = cfg.darken[i] > 0 ? PHASE_A_VSH1 : PHASE_A_VSL;
    for (size_t g = 0; g < levelCount && levels[g] <= m; g++) {
      out[ROW_OF_CLASS[i] * VS_ROW_BYTES + g] = phaseA;
    }
  }
  const uint8_t fr = static_cast<uint8_t>(cfg.frameRate & 0x0F);
  for (size_t i = 0; i < FR_BYTES; i++) out[FR_OFFSET + i] = static_cast<uint8_t>((fr << 4) | fr);
  memcpy(out + VOLTAGE_OFFSET, cfg.voltages, sizeof(cfg.voltages));
}

bool validateLut(const uint8_t lut[LUT_SIZE], char* error, size_t errorLen) {
  if (error && errorLen) error[0] = '\0';
  if (!lut) return false;
  for (size_t i = 0; i < 5; i++) {
    if (voltageCodeValid(i, lut[VOLTAGE_OFFSET + i])) continue;
    if (error && errorLen)
      snprintf(error, errorLen, "voltage byte %u outside the SSD1677 range", static_cast<unsigned>(i));
    return false;
  }
  unsigned frames = 0;
  for (size_t g = 0; g < 10; g++) {
    const uint8_t* tp = lut + TP_OFFSET + g * TP_GROUP_BYTES;
    frames += (static_cast<unsigned>(tp[0]) + tp[1] + tp[2] + tp[3]) * (static_cast<unsigned>(tp[4]) + 1);
  }
  if (frames > MAX_WAVEFORM_FRAMES) {
    if (error && errorLen) snprintf(error, errorLen, "waveform is %u frames, limit %u", frames, MAX_WAVEFORM_FRAMES);
    return false;
  }
  return true;
}

Source loadFromStorage(uint8_t out[LUT_SIZE]) {
  HalFile file;
  char reason[48];
  if (Storage.exists(RAW_LUT_PATH) && Storage.openFileForRead("G16", RAW_LUT_PATH, file)) {
    uint8_t raw[LUT_SIZE];
    const size_t size = file.size();
    const bool ok = size == LUT_SIZE && file.read(raw, LUT_SIZE) == static_cast<int>(LUT_SIZE);
    file.close();
    if (ok && validateLut(raw, reason, sizeof(reason))) {
      memcpy(out, raw, LUT_SIZE);
      LOG_INF("G16", "Fine LUT: %s", RAW_LUT_PATH);
      return Source::RawFile;
    }
    if (ok) {
      LOG_ERR("G16", "%s: %s; trying the config", RAW_LUT_PATH, reason);
    } else {
      LOG_ERR("G16", "%s: need exactly %u bytes, have %u; trying the config", RAW_LUT_PATH,
              static_cast<unsigned>(LUT_SIZE), static_cast<unsigned>(size));
    }
  }
  if (!Storage.exists(CONFIG_PATH)) {
    LOG_DBG("G16", "No fine LUT on the card; 16-level pages stay off");
    return Source::None;
  }
  if (!Storage.openFileForRead("G16", CONFIG_PATH, file)) return Source::None;
  // Boot and lab-time only: the config is a few short lines, read whole.
  char text[CONFIG_MAX_BYTES];
  const size_t size = file.size();
  const int got = size <= sizeof(text) ? file.read(text, size) : -1;
  file.close();
  if (got < 0 || static_cast<size_t>(got) != size) {
    LOG_ERR("G16", "%s: %u bytes (limit %u) or short read", CONFIG_PATH, static_cast<unsigned>(size),
            static_cast<unsigned>(sizeof(text)));
    return Source::None;
  }
  FineConfig cfg;
  if (!parseConfig(text, size, &cfg, reason, sizeof(reason))) {
    LOG_ERR("G16", "%s: %s", CONFIG_PATH, reason);
    return Source::None;
  }
  buildLut(cfg, out);
  LOG_INF("G16", "Fine LUT from %s: darken %d %d %d, fr %u, VGH %02X VSH1 %02X VSH2 %02X VSL %02X VCOM %02X",
          CONFIG_PATH, cfg.darken[0], cfg.darken[1], cfg.darken[2], cfg.frameRate, cfg.voltages[0], cfg.voltages[1],
          cfg.voltages[2], cfg.voltages[3], cfg.voltages[4]);
  return Source::Config;
}

}  // namespace gray16lut
