# 16-level grayscale pages and the comics (CBZ) reader

Fork feature. The X4's SSD1677 panel controller runs programmable waveforms
(see `freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h`), and
holds two 1-bit images per refresh, so one refresh can treat pixels as four
classes. Two refreshes with different waveforms (coarse, then fine) give
4 x 4 = 16 brightness levels. This document is the contract between the
pieces that make that work: the plane file format, the HAL protocol, and the
reader behaviour.

Phase 1 (this document) implements everything in the simulator and the
firmware's host-portable code; the device HAL reports "unsupported" and the
reader falls back to the existing 4-level path. Phase 2 adds an on-device
waveform lab; phase 3 wires the real two-pass waveforms into the device HAL.

## Data flow

```
page image (JPEG/PNG inside the .cbz)
  -> extracted to the cache dir            (ZipFile::readFileToStream)
  -> decoded, scaled to fit the page       (Jpeg/PngToBmpConverter, 8-bit gray BMP)
  -> rows read back as 8-bit luminance     (Bitmap::readNextRowLum)
  -> dithered to 16 levels, packed into    (Gray16Writer)
     four 1-bit planes in panel layout
  -> cached as pages/NNNN.g16              (192 KB per page)

display: for each pass (coarse, fine): load two planes into the framebuffer
         (Gray16Reader), draw the status bar, upload as LSB/MSB, run the pass
```

A full 4-bit page is 192 KB and does not fit in the X4's RAM, so planes are
cached on the SD card and streamed one at a time through the shared 48 KB
framebuffer. The writer holds one band of all four planes (15 KB in portrait,
25.6 KB in landscape); the reader holds one tile (3.8 KB or 6.4 KB).

## Brightness and bit conventions

- Brightness `g` is 0 (black) .. 15 (white). Plane `k` holds bit `k` of `g`.
  White is all four planes set, black all clear; this is the framebuffer's
  "bit set = white" convention, so a plane can be uploaded as-is.
- Bytes hold 8 pixels, MSB first (bit 7 = leftmost physical column), rows are
  `panelWidth / 8` bytes, exactly like the framebuffer.
- Pass 0 (coarse) uses planes 2 (LSB) and 3 (MSB); pass 1 (fine) uses planes
  0 (LSB) and 1 (MSB). Within a pass the 2-bit value `v = (MSB << 1) | LSB`
  means "0 = most darkening, 3 = none". This is the SDK's absolute-plane
  encoding (black = 00, dark = 10, light = 01, white = 11 in (LSB, MSB)
  order), so the existing `copyGrayscaleLsbBuffers` / `copyGrayscaleMsbBuffers`
  uploads carry the planes unchanged.
- Ideal result after both passes: luminance `= g * 17` (0 .. 255). The
  simulator models pass 0 as `lum = 255 - (3 - v) * 68` and pass 1 as
  `lum -= (3 - v) * 17`.
- 4-level fallback (panels without 16-level support): `q = (bit3 << 1) | bit2`
  (0 black, 1 dark, 2 light, 3 white). Base frame = plane 3 (bit set = white).
  Overlay masks (bit set = marked): `LSB = ~plane3 & plane2` (dark),
  `MSB = plane3 ^ plane2` (dark or light). Matches `grayPlanePixel()` and the
  XTCH reader.

## Plane file format (`.g16`, version 1)

Little-endian. Header, 32 bytes:

| offset | type   | field        | notes                                              |
|-------:|--------|--------------|----------------------------------------------------|
| 0      | char[4]| magic        | `G16P`                                             |
| 4      | u8     | version      | 1                                                  |
| 5      | u8     | orientation  | `GfxRenderer::Orientation` value the planes are baked for |
| 6      | u16    | panelWidth   | physical panel width (800 on the X4)               |
| 8      | u16    | panelHeight  | physical panel height (480)                        |
| 10     | u16    | bandRows     | logical rows per band (64)                         |
| 12     | u16    | pageWidth    | logical page width (480 in portrait)               |
| 14     | u16    | pageHeight   | logical page height (800)                          |
| 16     | u16    | imageX       | placed image rectangle, logical coordinates        |
| 18     | u16    | imageY       |                                                    |
| 20     | u16    | imageWidth   |                                                    |
| 22     | u16    | imageHeight  |                                                    |
| 24     | u8[8]  | reserved     | zero                                               |

Body: bands in logical-row order (`ceil(pageHeight / bandRows)` of them);
within each band, planes k = 0, 1, 2, 3. Each tile holds the physical
rectangle covered by that band's logical rows, row-major in physical
coordinates with stride `tileWidth / 8`. The rectangle follows from the
orientation, using the same mapping as `GfxRenderer`'s `rotateCoordinates`:

| orientation               | logical rows [y0, y1) map to                          | tile                       |
|---------------------------|-------------------------------------------------------|----------------------------|
| Portrait                  | physical columns [y0, y1), all rows                   | panelHeight x (y1 - y0)    |
| PortraitInverted          | physical columns [panelWidth - y1, panelWidth - y0)   | panelHeight x (y1 - y0)    |
| LandscapeCounterClockwise | physical rows [y0, y1), all columns                   | (y1 - y0) x panelWidth     |
| LandscapeClockwise        | physical rows [panelHeight - y1, panelHeight - y0)    | (y1 - y0) x panelWidth     |

`bandRows` must be a multiple of 8 so portrait tiles stay byte-aligned. Pixels
outside the image rectangle are white. Loading plane k is: for each band, read
one tile and copy each tile row into the framebuffer at its physical offset.

Orientation is baked in: if the reader's orientation differs from the file's,
the page is regenerated.

## HAL protocol (`HalDisplay`)

```
bool supportsGray16() const;
bool beginGray16();                    // false when unsupported -> use the 4-level fallback
void displayGray16Pass(uint8_t pass);  // 0 = coarse (planes 2,3), 1 = fine (planes 0,1)
void endGray16(const uint8_t* bwBaseline);
```

Sequence: `beginGray16()`; for each pass: `copyGrayscaleLsbBuffers(low plane)`,
`copyGrayscaleMsbBuffers(high plane)`, `displayGray16Pass(pass)`; then
`endGray16(bw)` with a B/W approximation of the page (plane 3), which restores
the controller's differential baseline so the next ordinary refresh is clean.

Device (phase 3): each pass maps to `EInkDisplay::displayGrayBuffer(turnOff,
lut, /*factoryMode=*/true)` with a coarse or fine LUT; the coarse LUT must start
from white (it is the first thing drawn on the page). Until then the device HAL
returns `false` from `beginGray16()`.

## CBZ reader

- A `.cbz` is a ZIP of page images. Entries are filtered to `.jpg`, `.jpeg`,
  `.png`, skipping directories, `__MACOSX/`, and names starting with `.`; the
  remaining names are sorted naturally (`2.jpg` before `10.jpg`), compared
  case-insensitively.
- Cache: `/.crosspoint/cbz_<hash>/` with `pages/NNNN.g16`, cover BMPs named
  like the EPUB ones (`cover_legacy_v2.bmp`, `_crop`, `cover_original...`;
  2-bit, sleep screen), `thumb_<height>.bmp` (1-bit, home screen) and
  `progress.bin` (current page), following the XTC reader's conventions. A
  page file is written as `NNNN.g16.tmp` and renamed into place, so a power
  loss cannot leave a truncated page behind.
- Title is the file name without extension; there is no author.
- First view of a page decodes it (a "Loading" popup is shown) and caches the
  planes; later views only stream the planes. The decoder scales the image to
  fit the logical page (aspect preserved, up or down); the result is placed
  centred as decoded (clamped to the page, never re-scaled), with white around
  it. The reader follows the orientation setting like the other readers; a
  cached page whose orientation differs is regenerated.
- The status bar follows the XTC status-bar setting (top/bottom/hidden) and is
  drawn into every plane as a white box with black text so it reads as pure
  black and white in the final image; the 4-level fallback clears its area
  from the overlay masks.
- Images that cannot be decoded (other formats, corrupt files) show a page
  error; the rest of the book stays readable.

## Costs and limits

- Cache: 192 KB per page. A 200-page volume uses about 38 MB of SD space.
- First view: JPEG decode plus two SD passes, roughly 2-3 s on the X4 (to be
  measured). Later views: four 48 KB reads plus the refreshes.
- Display time on the device is two absolute passes; expected ~1 s per page
  once phase 3 lands (to be measured).
- Images are scaled by the existing converters' nearest/linear scaling; no
  rotation of landscape pages onto a portrait screen (they are letterboxed).

## Tests

- `test/gray16`: dither range and determinism, writer/reader round trip on a
  small synthetic panel for all four orientations, letterbox padding, header
  validation.
- `test/cbz`: entry filtering and natural sort, page enumeration from a
  synthetic ZIP.
- Simulator: open a fixture `.cbz` with a 16-step gradient page, check the
  presented frame holds exactly the 16 luminances `g * 17`, letterboxing and
  page turns (native and browser builds); the 4-level fallback by setting
  `CROSSPOINT_SIM_NO_GRAY16=1`.
