#pragma once

#include <ConverterStub.h>
#include <HalStorage.h>

// Host stub of the firmware converter; see ConverterStub.h.
class JpegToBmpConverter {
 public:
  static bool jpegFileToBmpStream(HalFile& jpegFile, Print& bmpOut, bool = true, bool = false) {
    return converterStub::convert(jpegFile, bmpOut, 0, 0, false);
  }
  static bool jpegFileToBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int maxWidth, int maxHeight) {
    return converterStub::convert(jpegFile, bmpOut, maxWidth, maxHeight, false);
  }
  static bool jpegFileTo1BitBmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int maxWidth, int maxHeight) {
    return converterStub::convert(jpegFile, bmpOut, maxWidth, maxHeight, false);
  }
  static bool jpegFileToGray8BmpStreamWithSize(HalFile& jpegFile, Print& bmpOut, int maxWidth, int maxHeight) {
    return converterStub::convert(jpegFile, bmpOut, maxWidth, maxHeight, true);
  }
};
