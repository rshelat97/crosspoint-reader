#include "Gray16.h"

#include <Memory.h>

#include <cstring>
#include <utility>

namespace gray16 {

namespace {

constexpr uint32_t PIXELS_PER_BYTE = 8;

// Error rows hold luminance in 1/16 units, so the diffused shares round to
// 1/32 of a luminance step instead of to whole steps (a brightness level is
// only 17 luminance units wide, so whole-step truncation would drop much of
// the error).
constexpr int ERR_SHIFT = 4;
constexpr int ERR_HALF = 1 << (ERR_SHIFT - 1);
constexpr int LEVEL_STEP = 17 << ERR_SHIFT;  // one brightness level, scaled
constexpr int MAX_LUM = 255 << ERR_SHIFT;

void putU16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFF);
  out[1] = static_cast<uint8_t>(value >> 8);
}

uint16_t getU16(const uint8_t* in) { return static_cast<uint16_t>(in[0] | (in[1] << 8)); }

uint32_t tileByteCount(const Rect& tile) { return (static_cast<uint32_t>(tile.w) / PIXELS_PER_BYTE) * tile.h; }

}  // namespace

// ---------------------------------------------------------------------------
// PageSpec

void PageSpec::logicalSize(uint8_t orientation, uint16_t panelWidth, uint16_t panelHeight, uint16_t* w, uint16_t* h) {
  const bool portrait = orientation == Portrait || orientation == PortraitInverted;
  *w = portrait ? panelHeight : panelWidth;
  *h = portrait ? panelWidth : panelHeight;
}

bool PageSpec::valid() const {
  if (orientation > LandscapeCounterClockwise) return false;
  if (panelWidth == 0 || panelHeight == 0 || (panelWidth % PIXELS_PER_BYTE) != 0) return false;
  if (bandRows == 0 || (bandRows % PIXELS_PER_BYTE) != 0) return false;
  uint16_t w = 0;
  uint16_t h = 0;
  logicalSize(orientation, panelWidth, panelHeight, &w, &h);
  if (pageWidth != w || pageHeight != h) return false;
  // Portrait tiles are (y1 - y0) pixels wide; pageHeight == panelWidth and
  // bandRows are both multiples of 8, so every tile stays byte-aligned.
  return static_cast<uint32_t>(imageX) + imageWidth <= pageWidth &&
         static_cast<uint32_t>(imageY) + imageHeight <= pageHeight;
}

uint16_t PageSpec::bandCount() const {
  if (bandRows == 0) return 0;
  return static_cast<uint16_t>((static_cast<uint32_t>(pageHeight) + bandRows - 1) / bandRows);
}

bool PageSpec::tileRect(uint16_t band, Rect* out) const {
  if (out == nullptr || band >= bandCount()) return false;
  const uint32_t y0 = static_cast<uint32_t>(band) * bandRows;
  const uint32_t y1 = (y0 + bandRows < pageHeight) ? y0 + bandRows : pageHeight;
  const uint16_t rows = static_cast<uint16_t>(y1 - y0);
  switch (orientation) {
    case Portrait:
      out->x = static_cast<uint16_t>(y0);
      out->y = 0;
      out->w = rows;
      out->h = panelHeight;
      return true;
    case PortraitInverted:
      out->x = static_cast<uint16_t>(panelWidth - y1);
      out->y = 0;
      out->w = rows;
      out->h = panelHeight;
      return true;
    case LandscapeCounterClockwise:
      out->x = 0;
      out->y = static_cast<uint16_t>(y0);
      out->w = panelWidth;
      out->h = rows;
      return true;
    case LandscapeClockwise:
      out->x = 0;
      out->y = static_cast<uint16_t>(panelHeight - y1);
      out->w = panelWidth;
      out->h = rows;
      return true;
    default:
      return false;
  }
}

uint32_t PageSpec::maxTileBytes() const {
  // Only the last band can be partial, so band 0 is always a largest tile.
  Rect tile;
  if (!tileRect(0, &tile)) return 0;
  return tileByteCount(tile);
}

// ---------------------------------------------------------------------------
// Free functions

void logicalToPhysical(uint8_t orientation, uint16_t panelWidth, uint16_t panelHeight, int x, int y, int* phyX,
                       int* phyY) {
  switch (orientation) {
    case Portrait:
      *phyX = y;
      *phyY = panelHeight - 1 - x;
      break;
    case LandscapeClockwise:
      *phyX = panelWidth - 1 - x;
      *phyY = panelHeight - 1 - y;
      break;
    case PortraitInverted:
      *phyX = panelWidth - 1 - y;
      *phyY = x;
      break;
    case LandscapeCounterClockwise:
    default:
      *phyX = x;
      *phyY = y;
      break;
  }
}

bool fitImage(uint16_t srcW, uint16_t srcH, PageSpec* spec) {
  if (spec == nullptr || srcW == 0 || srcH == 0 || spec->pageWidth == 0 || spec->pageHeight == 0) return false;
  const uint32_t pageW = spec->pageWidth;
  const uint32_t pageH = spec->pageHeight;
  // Fill the width; when that makes the image too tall, fill the height
  // instead (the width then comes out below pageW).
  uint32_t w = pageW;
  uint32_t h = (static_cast<uint32_t>(srcH) * pageW) / srcW;
  if (h > pageH) {
    h = pageH;
    w = (static_cast<uint32_t>(srcW) * pageH) / srcH;
  }
  if (w == 0) w = 1;
  if (h == 0) h = 1;
  spec->imageWidth = static_cast<uint16_t>(w);
  spec->imageHeight = static_cast<uint16_t>(h);
  spec->imageX = static_cast<uint16_t>((pageW - w) / 2);
  spec->imageY = static_cast<uint16_t>((pageH - h) / 2);
  return true;
}

void encodeHeader(const PageSpec& spec, uint8_t out[HEADER_SIZE]) {
  memset(out, 0, HEADER_SIZE);
  memcpy(out, MAGIC, sizeof(MAGIC));
  out[4] = VERSION;
  out[5] = spec.orientation;
  putU16(out + 6, spec.panelWidth);
  putU16(out + 8, spec.panelHeight);
  putU16(out + 10, spec.bandRows);
  putU16(out + 12, spec.pageWidth);
  putU16(out + 14, spec.pageHeight);
  putU16(out + 16, spec.imageX);
  putU16(out + 18, spec.imageY);
  putU16(out + 20, spec.imageWidth);
  putU16(out + 22, spec.imageHeight);
}

bool decodeHeader(const uint8_t in[HEADER_SIZE], PageSpec* spec) {
  if (in == nullptr || spec == nullptr) return false;
  if (memcmp(in, MAGIC, sizeof(MAGIC)) != 0 || in[4] != VERSION) return false;
  PageSpec decoded;
  decoded.orientation = in[5];
  decoded.panelWidth = getU16(in + 6);
  decoded.panelHeight = getU16(in + 8);
  decoded.bandRows = getU16(in + 10);
  decoded.pageWidth = getU16(in + 12);
  decoded.pageHeight = getU16(in + 14);
  decoded.imageX = getU16(in + 16);
  decoded.imageY = getU16(in + 18);
  decoded.imageWidth = getU16(in + 20);
  decoded.imageHeight = getU16(in + 22);
  if (!decoded.valid()) return false;
  *spec = decoded;
  return true;
}

// ---------------------------------------------------------------------------
// Ditherer

Ditherer::Ditherer(uint16_t width) : width(width) {
  if (width == 0) return;
  const size_t entries = static_cast<size_t>(width) + 2;
  // Value-initialized, so the rows start out at zero.
  errCur = makeUniqueNoThrow<int16_t[]>(entries);
  errNext = makeUniqueNoThrow<int16_t[]>(entries);
  if (!errCur || !errNext) {
    errCur.reset();  // isValid() reports through errCur alone
    errNext.reset();
  }
}

Ditherer::~Ditherer() = default;

uint8_t Ditherer::processPixel(uint8_t lum, uint16_t x) {
  int adjusted = lum << ERR_SHIFT;
  if (errCur != nullptr) adjusted += errCur[x + 1];
  if (adjusted < 0) {
    adjusted = 0;
  } else if (adjusted > MAX_LUM) {
    adjusted = MAX_LUM;
  }
  const int level = (adjusted + LEVEL_STEP / 2) / LEVEL_STEP;  // nearest level
  if (errCur == nullptr) return static_cast<uint8_t>(level);
  const int err = adjusted - level * LEVEL_STEP;  // |err| <= LEVEL_STEP / 2
  // Shares round to nearest; the 7/16 share takes the remainder so the four
  // shares always sum to err.
  const int e1 = (err + ERR_HALF) >> ERR_SHIFT;
  const int e3 = (err * 3 + ERR_HALF) >> ERR_SHIFT;
  const int e5 = (err * 5 + ERR_HALF) >> ERR_SHIFT;
  errCur[x + 2] = static_cast<int16_t>(errCur[x + 2] + (err - e1 - e3 - e5));
  errNext[x] = static_cast<int16_t>(errNext[x] + e3);
  errNext[x + 1] = static_cast<int16_t>(errNext[x + 1] + e5);
  errNext[x + 2] = static_cast<int16_t>(errNext[x + 2] + e1);
  return static_cast<uint8_t>(level);
}

void Ditherer::nextRow() {
  if (errCur == nullptr) return;
  std::swap(errCur, errNext);
  memset(errNext.get(), 0, (static_cast<size_t>(width) + 2) * sizeof(int16_t));
}

void Ditherer::reset() {
  if (errCur == nullptr) return;
  const size_t bytes = (static_cast<size_t>(width) + 2) * sizeof(int16_t);
  memset(errCur.get(), 0, bytes);
  memset(errNext.get(), 0, bytes);
}

// ---------------------------------------------------------------------------
// Writer

Writer::Writer(const PageSpec& spec) : pageSpec(spec) {
  if (!spec.valid()) return;
  const uint32_t maxBytes = spec.maxTileBytes();
  if (maxBytes == 0) return;
  bandTiles = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(PLANE_COUNT) * maxBytes);
  if (!bandTiles) return;
  if (spec.imageWidth > 0) {
    ditherer = makeUniqueNoThrow<Ditherer>(spec.imageWidth);
    if (!ditherer || !ditherer->isValid()) {
      ditherer.reset();
      bandTiles.reset();
      return;
    }
  }
  valid = true;
}

Writer::~Writer() = default;

bool Writer::isValid() const { return valid; }

bool Writer::begin(WriteFn write, void* ctx) {
  if (!valid || write == nullptr || (begun && !failed)) return false;
  begun = false;
  failed = false;
  uint8_t header[HEADER_SIZE];
  encodeHeader(pageSpec, header);
  if (!write(ctx, header, HEADER_SIZE)) return false;
  writeFn = write;
  writeCtx = ctx;
  currentBand = 0;
  pageRow = 0;
  imageRow = 0;
  if (ditherer != nullptr) ditherer->reset();
  begun = startBand();
  return begun;
}

bool Writer::addImageRow(const uint8_t* lum8) {
  if (!begun || failed || lum8 == nullptr || imageRow >= pageSpec.imageHeight) return false;
  while (pageRow < pageSpec.imageY) {
    if (!emitPageRow(nullptr)) {
      failed = true;
      return false;
    }
  }
  if (!emitPageRow(lum8)) {
    failed = true;
    return false;
  }
  ++imageRow;
  return true;
}

bool Writer::finish() {
  if (!begun || failed) return false;
  while (pageRow < pageSpec.pageHeight) {
    if (!emitPageRow(nullptr)) {
      failed = true;
      return false;
    }
  }
  begun = false;
  return imageRow == pageSpec.imageHeight;
}

bool Writer::startBand() {
  Rect tile;
  if (!pageSpec.tileRect(currentBand, &tile)) return false;
  tileBytes = tileByteCount(tile);
  memset(bandTiles.get(), 0xFF, static_cast<size_t>(PLANE_COUNT) * tileBytes);
  return true;
}

bool Writer::emitPageRow(const uint8_t* lum8OrNull) {
  if (pageRow >= pageSpec.pageHeight) return false;
  Rect tile;
  if (!pageSpec.tileRect(currentBand, &tile)) return false;
  if (lum8OrNull != nullptr && ditherer != nullptr) {
    const uint32_t stride = tile.w / PIXELS_PER_BYTE;
    // The logical->physical mapping is a rotation, so stepping along the
    // image row moves the physical position by a constant (dx, dy).
    int phyX = 0;
    int phyY = 0;
    int nextX = 0;
    int nextY = 0;
    logicalToPhysical(pageSpec.orientation, pageSpec.panelWidth, pageSpec.panelHeight, pageSpec.imageX, pageRow, &phyX,
                      &phyY);
    logicalToPhysical(pageSpec.orientation, pageSpec.panelWidth, pageSpec.panelHeight, pageSpec.imageX + 1, pageRow,
                      &nextX, &nextY);
    const int dx = nextX - phyX;
    const int dy = nextY - phyY;
    for (uint16_t i = 0; i < pageSpec.imageWidth; ++i, phyX += dx, phyY += dy) {
      const uint8_t g = ditherer->processPixel(lum8OrNull[i], i);
      if (g == LEVELS - 1) continue;  // white: every plane bit stays set
      const uint32_t tx = static_cast<uint32_t>(phyX - tile.x);
      const uint32_t ty = static_cast<uint32_t>(phyY - tile.y);
      const uint32_t index = ty * stride + tx / PIXELS_PER_BYTE;
      const uint8_t keepMask = static_cast<uint8_t>(~(0x80u >> (tx % PIXELS_PER_BYTE)));
      uint8_t* plane = bandTiles.get();
      for (uint8_t k = 0; k < PLANE_COUNT; ++k, plane += tileBytes) {
        if (((g >> k) & 1) == 0) plane[index] &= keepMask;
      }
    }
    ditherer->nextRow();
  }
  ++pageRow;
  const uint32_t bandEnd = static_cast<uint32_t>(currentBand + 1) * pageSpec.bandRows;
  if (pageRow < bandEnd && pageRow < pageSpec.pageHeight) return true;
  return flushBand();
}

bool Writer::flushBand() {
  // The tiles sit back to back in plane order, so the band is one write.
  if (!writeFn(writeCtx, bandTiles.get(), static_cast<size_t>(PLANE_COUNT) * tileBytes)) return false;
  ++currentBand;
  if (currentBand < pageSpec.bandCount()) return startBand();
  return true;
}

// ---------------------------------------------------------------------------
// Reader

bool Reader::open(ReadAtFn readAt, void* ctx) {
  opened = false;
  if (readAt == nullptr) return false;
  uint8_t header[HEADER_SIZE];
  if (!readAt(ctx, 0, header, HEADER_SIZE)) return false;
  if (!decodeHeader(header, &pageSpec)) return false;
  readFn = readAt;
  readCtx = ctx;
  opened = true;
  return true;
}

uint32_t Reader::tileOffset(uint16_t band, uint8_t k) const {
  Rect tile;
  if (!opened || k >= PLANE_COUNT || !pageSpec.tileRect(band, &tile)) return 0;
  // Every band before `band` is full-size: only the last band can be partial.
  const uint32_t bandBytes = static_cast<uint32_t>(PLANE_COUNT) * pageSpec.maxTileBytes();
  return static_cast<uint32_t>(HEADER_SIZE) + band * bandBytes + k * tileByteCount(tile);
}

bool Reader::loadPlane(uint8_t k, uint8_t* framebuffer, uint8_t* scratch, size_t scratchLen) const {
  if (!opened || k >= PLANE_COUNT || framebuffer == nullptr || scratch == nullptr) return false;
  if (scratchLen < pageSpec.maxTileBytes()) return false;
  const uint32_t fbStride = pageSpec.panelWidth / PIXELS_PER_BYTE;
  const uint16_t bands = pageSpec.bandCount();
  for (uint16_t band = 0; band < bands; ++band) {
    Rect tile;
    if (!pageSpec.tileRect(band, &tile)) return false;
    const uint32_t rowBytes = tile.w / PIXELS_PER_BYTE;
    if (!readFn(readCtx, tileOffset(band, k), scratch, rowBytes * tile.h)) return false;
    uint8_t* dst = framebuffer + static_cast<size_t>(tile.y) * fbStride + tile.x / PIXELS_PER_BYTE;
    const uint8_t* src = scratch;
    for (uint16_t row = 0; row < tile.h; ++row, dst += fbStride, src += rowBytes) {
      memcpy(dst, src, rowBytes);
    }
  }
  return true;
}

}  // namespace gray16
