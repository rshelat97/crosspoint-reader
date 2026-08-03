#pragma once
// Simulator fallback for bitbank2/PNGdec when the real library cannot be
// fetched (offline build). open() fails, so PNG images take the firmware's
// decode-failure path. The CMake build prefers the real PNGdec whenever the
// network allows.
#include <cstdint>

#define PNG_SUCCESS 0
#define PNG_PIXEL_GRAYSCALE 0
#define PNG_PIXEL_TRUECOLOR 2
#define PNG_PIXEL_INDEXED 3
#define PNG_PIXEL_GRAY_ALPHA 4
#define PNG_PIXEL_TRUECOLOR_ALPHA 6
#define PNG_MAX_BUFFERED_PIXELS 4096

struct PNGFILE {
  void* fHandle;
};

struct PNGDRAW {
  int y;
  int iWidth;
  int iPitch;
  int iPixelType;
  int iBpp;
  int iHasAlpha;
  uint8_t* pPixels;
  uint8_t* pPalette;
  void* pUser;
};

typedef int(PNG_DRAW_CALLBACK)(PNGDRAW* pDraw);
typedef void*(PNG_OPEN_CALLBACK)(const char* szFilename, int32_t* pFileSize);
typedef void(PNG_CLOSE_CALLBACK)(void* pHandle);
typedef int32_t(PNG_READ_CALLBACK)(PNGFILE* pFile, uint8_t* pBuf, int32_t iLen);
typedef int32_t(PNG_SEEK_CALLBACK)(PNGFILE* pFile, int32_t iPosition);

class PNG {
 public:
  int open(const char*, PNG_OPEN_CALLBACK*, PNG_CLOSE_CALLBACK*, PNG_READ_CALLBACK*, PNG_SEEK_CALLBACK*,
           PNG_DRAW_CALLBACK*) {
    return -1;  // failure: no decoder in this build
  }
  void close() {}
  int decode(void*, int) { return -1; }
  int getWidth() const { return 0; }
  int getHeight() const { return 0; }
  int getBpp() const { return 0; }
  int getPixelType() const { return 0; }
};
