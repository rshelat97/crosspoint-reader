#pragma once
// Simulator fallback for bitbank2/JPEGDEC when the real library cannot be
// fetched (offline build). open() fails, so JPEG images take the firmware's
// decode-failure path (placeholder rendering). The CMake build prefers the
// real JPEGDEC via FetchContent whenever the network allows. Types mirror the
// real library's so firmware callback signatures compile unchanged.
#include <cstdint>

#define EIGHT_BIT_GRAYSCALE 0
#define RGB565_LITTLE_ENDIAN 1
#define JPEG_SCALE_HALF 2
#define JPEG_SCALE_QUARTER 4
#define JPEG_SCALE_EIGHTH 8
#define JPEG_MODE_BASELINE 0
#define JPEG_MODE_PROGRESSIVE 1

typedef struct jpeg_file_tag {
  int32_t iPos;
  int32_t iSize;
  uint8_t* pData;
  void* fHandle;
} JPEGFILE;

typedef struct jpeg_draw_tag {
  int x, y;
  int iWidth, iHeight;
  int iWidthUsed;
  int iBpp;
  uint16_t* pPixels;
  void* pUser;
} JPEGDRAW;

typedef int(JPEG_DRAW_CALLBACK)(JPEGDRAW* pDraw);
typedef void*(JPEG_OPEN_CALLBACK)(const char* szFilename, int32_t* pFileSize);
typedef void(JPEG_CLOSE_CALLBACK)(void* pHandle);
typedef int32_t(JPEG_READ_CALLBACK)(JPEGFILE* pFile, uint8_t* pBuf, int32_t iLen);
typedef int32_t(JPEG_SEEK_CALLBACK)(JPEGFILE* pFile, int32_t iPosition);

class JPEGDEC {
 public:
  int open(const char*, JPEG_OPEN_CALLBACK*, JPEG_CLOSE_CALLBACK*, JPEG_READ_CALLBACK*, JPEG_SEEK_CALLBACK*,
           JPEG_DRAW_CALLBACK*) {
    return 0;  // failure: no decoder in this build
  }
  int open(const uint8_t*, int, JPEG_DRAW_CALLBACK*) { return 0; }
  void close() {}
  int decode(int, int, int) { return 0; }
  int getWidth() const { return 0; }
  int getHeight() const { return 0; }
  int getJPEGType() const { return JPEG_MODE_BASELINE; }
  int getLastError() const { return -1; }
  void setPixelType(int) {}
  void setUserPointer(void*) {}
};
