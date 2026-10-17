#pragma once
// 16-level grayscale page planes: dither 8-bit rows to 4-bit brightness and
// pack them into four 1-bit planes laid out in physical panel coordinates
// (writer), and stream one plane back into a framebuffer (reader).
// Format, bit conventions and tile geometry: docs/comics-gray16.md.
//
// Host-portable: no Arduino, HAL or heap-tracking dependencies. I/O goes
// through caller-supplied functions so the same code runs in host tests, the
// simulator and on the device (HalFile adapters live in lib/Cbz).
#include <cstddef>
#include <cstdint>

namespace gray16 {

constexpr char MAGIC[4] = {'G', '1', '6', 'P'};
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_SIZE = 32;
constexpr uint16_t DEFAULT_BAND_ROWS = 64;
constexpr uint8_t PLANE_COUNT = 4;
constexpr uint8_t LEVELS = 16;

// Same values as GfxRenderer::Orientation; kept here so this library does not
// depend on the renderer. logicalToPhysical() must stay identical to
// GfxRenderer's rotateCoordinates().
enum Orientation : uint8_t {
  Portrait = 0,                   // logical 480x800 on an 800x480 panel
  LandscapeClockwise = 1,         // logical 800x480, rotated 180 degrees
  PortraitInverted = 2,           // logical 480x800, rotated the other way
  LandscapeCounterClockwise = 3,  // logical 800x480, panel-native
};

struct Rect {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t w = 0;
  uint16_t h = 0;
};

// Geometry of one page file. `pageWidth`/`pageHeight` are the logical page
// (the orientation's logical screen size); the image rectangle is placed
// inside it and everything outside is white.
struct PageSpec {
  uint8_t orientation = Portrait;
  uint16_t panelWidth = 0;   // physical, e.g. 800
  uint16_t panelHeight = 0;  // physical, e.g. 480
  uint16_t bandRows = DEFAULT_BAND_ROWS;
  uint16_t pageWidth = 0;
  uint16_t pageHeight = 0;
  uint16_t imageX = 0;
  uint16_t imageY = 0;
  uint16_t imageWidth = 0;
  uint16_t imageHeight = 0;

  // True when the spec is self-consistent: panel and page dims non-zero and
  // matching the orientation (portrait: pageWidth == panelHeight and
  // pageHeight == panelWidth; landscape: equal to the panel), panelWidth a
  // multiple of 8, bandRows a non-zero multiple of 8, image inside the page.
  bool valid() const;
  // Logical screen size for an orientation on this panel.
  static void logicalSize(uint8_t orientation, uint16_t panelWidth, uint16_t panelHeight, uint16_t* w, uint16_t* h);
  uint16_t bandCount() const;
  // Physical rectangle covered by band `band`'s logical rows (see the table in
  // docs/comics-gray16.md). Returns false for an out-of-range band.
  bool tileRect(uint16_t band, Rect* out) const;
  // Bytes of the largest tile of one plane (the reader's scratch size).
  uint32_t maxTileBytes() const;
};

// Logical -> physical pixel mapping, identical to GfxRenderer::rotateCoordinates.
void logicalToPhysical(uint8_t orientation, uint16_t panelWidth, uint16_t panelHeight, int x, int y, int* phyX,
                       int* phyY);

// Scale (srcW x srcH) to fit the page in `spec` preserving aspect ratio (up or
// down), centred: fills the image rectangle fields of `spec` from its page
// size. Returns false when a dimension is zero.
bool fitImage(uint16_t srcW, uint16_t srcH, PageSpec* spec);

// Encode / decode the 32-byte header. decode() validates magic, version and
// PageSpec::valid(); returns false on any mismatch.
void encodeHeader(const PageSpec& spec, uint8_t out[HEADER_SIZE]);
bool decodeHeader(const uint8_t in[HEADER_SIZE], PageSpec* spec);

// I/O callbacks (function pointer + context, not std::function: see CLAUDE.md).
using WriteFn = bool (*)(void* ctx, const uint8_t* data, size_t len);
using ReadAtFn = bool (*)(void* ctx, uint32_t offset, uint8_t* dst, size_t len);

// Floyd-Steinberg error diffusion to 16 levels. Deterministic. Rows are
// processed left to right; call nextRow() after each row.
class Ditherer {
 public:
  explicit Ditherer(uint16_t width);
  ~Ditherer();
  Ditherer(const Ditherer&) = delete;
  Ditherer& operator=(const Ditherer&) = delete;
  bool isValid() const { return errCur != nullptr; }
  // 8-bit luminance in, brightness 0..15 out.
  uint8_t processPixel(uint8_t lum, uint16_t x);
  void nextRow();
  void reset();

 private:
  uint16_t width;
  int16_t* errCur = nullptr;   // width + 2 entries, index x + 1
  int16_t* errNext = nullptr;  // width + 2 entries
};

// Streams one page: header, then band tiles. Feed exactly imageHeight image
// rows of imageWidth luminance bytes; rows outside the image are padded
// white. Allocation happens in the constructor (nothrow); check isValid().
class Writer {
 public:
  explicit Writer(const PageSpec& spec);
  ~Writer();
  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;
  bool isValid() const;
  const PageSpec& spec() const { return pageSpec; }

  bool begin(WriteFn write, void* ctx);  // writes the header
  bool addImageRow(const uint8_t* lum8);  // imageWidth bytes
  bool finish();                          // pads the rest white, flushes the last band

 private:
  bool emitPageRow(const uint8_t* lum8OrNull);  // null = white row
  bool flushBand();
  PageSpec pageSpec;
  WriteFn writeFn = nullptr;
  void* writeCtx = nullptr;
  uint8_t* bandTiles = nullptr;  // PLANE_COUNT tiles of the current band
  uint32_t tileBytes = 0;        // bytes per plane tile of the current band
  uint16_t currentBand = 0;
  uint16_t pageRow = 0;   // next logical row to emit
  uint16_t imageRow = 0;  // image rows received
  Ditherer* ditherer = nullptr;
  bool valid = false;
  bool begun = false;
};

// Reads planes from a page file through a random-access callback.
class Reader {
 public:
  // Reads and validates the header; false on I/O error or a bad header.
  bool open(ReadAtFn readAt, void* ctx);
  const PageSpec& spec() const { return pageSpec; }
  bool isOpen() const { return opened; }
  // Loads plane k (0..3) into a framebuffer of spec().panelWidth x
  // spec().panelHeight, stride panelWidth / 8, overwriting every byte it
  // covers. `scratch` must hold at least spec().maxTileBytes() bytes.
  bool loadPlane(uint8_t k, uint8_t* framebuffer, uint8_t* scratch, size_t scratchLen) const;
  // Byte offset of plane k's tile for `band`, for callers that stream tiles themselves.
  uint32_t tileOffset(uint16_t band, uint8_t k) const;

 private:
  PageSpec pageSpec;
  ReadAtFn readFn = nullptr;
  void* readCtx = nullptr;
  bool opened = false;
};

}  // namespace gray16
