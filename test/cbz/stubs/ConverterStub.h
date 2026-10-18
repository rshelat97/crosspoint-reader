#pragma once

// Shared state of the JPEG/PNG converter stubs. Instead of decoding, a stub
// records the call, snapshots its input and writes a synthetic BMP (an 8-bit
// grayscale one for the gray8 variant) so Cbz's file plumbing and the real
// Bitmap/Gray16 code run end to end on the host.
#include <HalStorage.h>

#include <cstdint>
#include <vector>

namespace converterStub {

inline bool succeed = true;  // result of the next conversions
inline int calls = 0;
inline int lastMaxWidth = 0;
inline int lastMaxHeight = 0;
inline std::vector<uint8_t> lastInput;  // bytes of the image handed to the converter
// Size and content of the synthetic gray8 BMP: zero width/height means "fill
// the requested maximum"; rowLum gives the luminance of every pixel in row y.
inline int gray8Width = 0;
inline int gray8Height = 0;
inline uint8_t (*rowLum)(int y) = nullptr;

inline void reset() {
  succeed = true;
  calls = 0;
  lastMaxWidth = 0;
  lastMaxHeight = 0;
  lastInput.clear();
  gray8Width = 0;
  gray8Height = 0;
  rowLum = nullptr;
}

inline void putLe16(Print& out, uint16_t value) {
  out.write(static_cast<uint8_t>(value));
  out.write(static_cast<uint8_t>(value >> 8));
}

inline void putLe32(Print& out, uint32_t value) {
  putLe16(out, static_cast<uint16_t>(value));
  putLe16(out, static_cast<uint16_t>(value >> 16));
}

// Same layout as the firmware's writeBmpHeader8bit: gray palette, top-down rows.
inline void writeGray8Bmp(Print& out, int width, int height) {
  const int bytesPerRow = (width + 3) / 4 * 4;
  const uint32_t paletteSize = 256 * 4;
  const uint32_t imageSize = static_cast<uint32_t>(bytesPerRow) * height;
  out.write('B');
  out.write('M');
  putLe32(out, 14 + 40 + paletteSize + imageSize);
  putLe32(out, 0);
  putLe32(out, 14 + 40 + paletteSize);
  putLe32(out, 40);
  putLe32(out, static_cast<uint32_t>(width));
  putLe32(out, static_cast<uint32_t>(-height));
  putLe16(out, 1);
  putLe16(out, 8);
  putLe32(out, 0);
  putLe32(out, imageSize);
  putLe32(out, 2835);
  putLe32(out, 2835);
  putLe32(out, 256);
  putLe32(out, 256);
  for (int i = 0; i < 256; i++) {
    const uint8_t gray = static_cast<uint8_t>(i);
    out.write(gray);
    out.write(gray);
    out.write(gray);
    out.write(static_cast<uint8_t>(0));
  }
  std::vector<uint8_t> row(static_cast<size_t>(bytesPerRow), 0);
  for (int y = 0; y < height; y++) {
    const uint8_t lum = rowLum ? rowLum(y) : 0;
    for (int x = 0; x < width; x++) row[x] = lum;
    out.write(row.data(), row.size());
  }
}

inline bool convert(HalFile& in, Print& out, int maxWidth, int maxHeight, bool gray8) {
  calls++;
  lastMaxWidth = maxWidth;
  lastMaxHeight = maxHeight;
  lastInput.assign(in.size(), 0);
  in.seek(0);
  in.read(lastInput.data(), lastInput.size());
  if (!succeed) return false;
  if (gray8) {
    writeGray8Bmp(out, gray8Width > 0 ? gray8Width : maxWidth, gray8Height > 0 ? gray8Height : maxHeight);
  } else {
    const uint8_t marker[3] = {'B', 'M', 'P'};
    out.write(marker, sizeof(marker));
  }
  return true;
}

}  // namespace converterStub
