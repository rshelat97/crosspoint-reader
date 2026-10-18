#pragma once

#include <cstddef>
#include <cstdint>

// Fine-pass waveform for 16-level pages on SSD1677 panels. A small text config
// (or a raw 112-byte LUT) on the SD card becomes the LUT that HalDisplay runs
// as pass 1 after the factory 4-level pass. Format, row mapping and tuning
// notes: docs/comics-gray16.md, "Phase 2".
namespace gray16lut {

constexpr size_t LUT_SIZE = 112;
constexpr const char* DIR = "/.crosspoint/gray16";
constexpr const char* CONFIG_PATH = "/.crosspoint/gray16/fine.cfg";
constexpr const char* RAW_LUT_PATH = "/.crosspoint/gray16/fine.lut";

struct FineConfig {
  // Extra frames driven after the coarse pass by the classes that need 1, 2
  // and 3 darkening steps (public values v = 2, 1, 0; v = 3 never drives).
  // Positive = VSH1 frames (darken), negative = VSL frames (lighten), 0 = none.
  int8_t darken[3] = {2, 4, 7};
  uint8_t frameRate = 0x2;                               // 4-bit FR code, all groups
  uint8_t voltages[5] = {0x17, 0x41, 0xA8, 0x32, 0x30};  // VGH, VSH1, VSH2, VSL, VCOM (X4 factory tail)
};

// Parses the "key = value" text format: "darken = 2 4 7", "fr = 2", and
// "vgh | vsh1 | vsh2 | vsl | vcom = 0x..". '#' starts a comment, blank lines
// are skipped, keys are case-insensitive. Unknown keys, missing values and
// out-of-range numbers fail; `error` (errorLen bytes) receives a short reason.
// Keys that are absent keep the defaults of `out`.
bool parseConfig(const char* text, size_t len, FineConfig* out, char* error, size_t errorLen);

// Builds the 112-byte LUT: five VS rows in controller order (row index =
// (BW-RAM bit << 1) | RED-RAM bit of the raw plane bits, so public value v
// maps to rows 3, 1, 2, 0 for v = 3, 2, 1, 0), up to three timing groups
// holding the pulse-length deltas in phase A with RP = 0, the frame-rate code
// in every group, then the voltage tail and two zero bytes. Deterministic.
void buildLut(const FineConfig& cfg, uint8_t out[LUT_SIZE]);

enum class Source : uint8_t { None, RawFile, Config };

// Loads the user's fine LUT from the SD card: fine.lut (exactly 112 bytes)
// wins, else fine.cfg is parsed and built. Returns None and leaves `out`
// untouched when neither exists or the file is invalid; the reason is logged.
Source loadFromStorage(uint8_t out[LUT_SIZE]);

}  // namespace gray16lut
