#pragma once

#include <ConverterStub.h>
#include <HalStorage.h>

// Host stub of the firmware converter; see ConverterStub.h.
class PngToBmpConverter {
 public:
  static bool pngFileToBmpStream(HalFile& pngFile, Print& bmpOut, bool = true, bool = false) {
    return converterStub::convert(pngFile, bmpOut, 0, 0, false);
  }
  static bool pngFileToBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int maxWidth, int maxHeight) {
    return converterStub::convert(pngFile, bmpOut, maxWidth, maxHeight, false);
  }
  static bool pngFileTo1BitBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int maxWidth, int maxHeight) {
    return converterStub::convert(pngFile, bmpOut, maxWidth, maxHeight, false);
  }
  static bool pngFileToGray8BmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int maxWidth, int maxHeight) {
    return converterStub::convert(pngFile, bmpOut, maxWidth, maxHeight, true);
  }
};
