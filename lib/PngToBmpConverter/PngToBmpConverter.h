#pragma once

#include <HalStorage.h>

class Print;

class PngToBmpConverter {
  static bool pngFileToBmpStreamInternal(HalFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                         bool oneBit, bool crop = true, bool originalThresholds = false,
                                         bool gray8 = false);

 public:
  static bool pngFileToBmpStream(HalFile& pngFile, Print& bmpOut, bool crop = true, bool originalThresholds = false);
  static bool pngFileToBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  static bool pngFileTo1BitBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
  // 8-bit grayscale BMP (gray palette, top-down, raw luminance) fitted inside
  // maxWidth x maxHeight; the PNG twin of JpegToBmpConverter::jpegFileToGray8BmpStreamWithSize.
  static bool pngFileToGray8BmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int maxWidth, int maxHeight);
};
