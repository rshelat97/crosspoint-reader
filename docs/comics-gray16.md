# 16-level grayscale pages and the comics (CBZ) reader

Fork feature. The X4's SSD1677 panel controller runs programmable waveforms
(see `freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h`), and
holds two 1-bit images per refresh, so one refresh can treat pixels as four
classes. Two refreshes with different waveforms (coarse, then fine) give
4 x 4 = 16 brightness levels. This document is the contract between the
pieces that make that work: the plane file format, the HAL protocol, and the
reader behaviour.

Phase 1 implemented everything in the simulator and the firmware's
host-portable code, with the device on the existing 4-level path. Phase 2 (the
section of that name below) adds the device two-pass path, driven by a fine
waveform file on the SD card, and a lab screen to tune it. Phase 3 bakes the
tuned waveform into the firmware and adds a measured tone curve.

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
         (Gray16Reader), draw the status bar, upload as LSB/MSB, run the pass;
         then load plane 3 once more as the B/W baseline for endGray16
```

A full 4-bit page is 192 KB and does not fit in the X4's RAM, so planes are
cached on the SD card and streamed one at a time through the shared 48 KB
framebuffer. The writer holds one band of all four planes (15 KB in portrait,
25.6 KB in landscape); the reader holds one tile (6.4 KB, allocated for the
activity's lifetime). The 4-level fallback also needs a second 48 KB plane
buffer; it is allocated per render, after the page has been decoded and
cached, and freed right after the refresh, so it never competes with the
decoder for RAM.

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
void displayGray16Pass(uint8_t pass, bool turnOffScreen = false);  // 0 = coarse (planes 2,3), 1 = fine (planes 0,1)
void endGray16(const uint8_t* bwBaseline);
```

Sequence: `beginGray16()` (no visible effect: it only claims the sequence, and
returns false where 16 levels are unsupported); for each pass:
`copyGrayscaleLsbBuffers(low plane)`,
`copyGrayscaleMsbBuffers(high plane)`, `displayGray16Pass(pass)`; then
`endGray16(bw)` with a B/W approximation of the page (plane 3), which restores
the controller's differential baseline so the next ordinary refresh is clean.

Activities go through the `GfxRenderer` wrappers (`supportsGray16`,
`beginGray16`, `displayGray16Pass`, `endGray16`), which pass the fading-fix
setting like the existing grayscale uploads do; they never call `HalDisplay`
directly. `endGray16` owns the cleanup of the grayscale buffers, so the
16-level path does not call `cleanupGrayscaleWithFrameBuffer`.

Device: each pass is one SDK factory grayscale refresh. The coarse pass runs
the SDK's factory 4-level LUT, which is absolute: it drives every pixel class
to its target from whatever ink the previous page left, so no white flush is
needed. The fine pass is relative: a few extra frames on top of the coarse
result, class by class. A page therefore costs exactly two refreshes. The
concrete sequence is in the "Phase 2" section; the SDK facts it rests on
(`freeink-sdk/libs/display/FreeInkDisplay/src/`):

- `FreeInkDisplay::displayGrayBuffer(turnOffScreen, lut, factoryMode)` is the
  facade entry that runs a custom LUT. In Absolute mode it cancels the pass
  when `lut != nullptr` (`FreeInkDisplay.cpp`, around line 880) and otherwise
  runs the driver with its own LUT, so custom coarse/fine LUTs need either
  the Overlay-mode factory path with a HAL-side plane complement, or an SDK
  extension. There is no direct driver route: the facade keeps `_driver`
  private.
- Only in Absolute mode does `Ssd1677Driver::writeGrayRam` complement each
  host plane (`Ssd1677Driver.cpp`, around line 783); on the Overlay-mode path
  the HAL must complement them itself. The plane bits in this document are
  host-side values either way.
- A factory-mode pass sets the driver's `_needsGrayClear` flag
  (`Ssd1677Driver.cpp`, around line 847): the next Fast refresh is promoted
  to a Half/Full clean, because restoring RAM alone cannot restore the B/W
  ink state. `endGray16(bw)` re-uploads the B/W baseline so that clean starts
  from the right previous image.

The device HAL returns `false` from `beginGray16()` until a fine LUT has been
set (`setGray16FineLut`), which the boot code does when the card holds one,
and right after an Absolute-mode pass (see the Phase 2 section).

## CBZ reader

- A `.cbz` is a ZIP of page images. Entries are filtered to `.jpg`, `.jpeg`,
  `.png`, skipping directories, anything under `__MACOSX/`, and entries whose
  base name starts with `.` (macOS `._` resource forks and the like); the
  remaining names are sorted naturally (`2.jpg` before `10.jpg`), compared
  case-insensitively. Entries with other extensions are not pages and are
  ignored.
- Cache: `/.crosspoint/cbz_<hash>/` with `pages/NNNN.g16`, cover BMPs named
  like the EPUB ones (`cover_legacy_v2.bmp`, `_crop`, `cover_original...`;
  2-bit, sleep screen), `thumb_<height>.bmp` (1-bit, home screen) and
  `progress.bin`, named like the XTC reader's files. `progress.bin`
  holds the current page and the page count as two little-endian `uint32_t`
  (a 4-byte file from earlier fork builds is still read; the home screen then
  counts the pages by opening the ZIP). A page file is written as
  `NNNN.g16.tmp` and renamed into place, so a power loss cannot leave a
  truncated page behind. A page file with a stale header (other orientation,
  panel size or band height) is regenerated when the page is next opened; a
  file whose body cannot be read is removed, that view shows the page error,
  and the next view rebuilds it. Pages are never evicted; deleting the book
  from the file browser removes the whole cache directory.
- Title is the file name without extension; there is no author.
- First view of a page decodes it (a "Loading" popup is shown) and caches the
  planes; later views only stream the planes. The decoder scales the image to
  fit the logical page (aspect preserved, up or down); the result is placed
  centred as decoded (clamped to the page, never re-scaled), with white around
  it. The reader follows the orientation setting like the other readers; a
  cached page whose orientation differs is regenerated.
- The status bar follows the XTC status-bar setting (shown as "XTC Status
  Bar" in Settings: top/bottom/hidden; no new setting is added) and is
  drawn into every plane as a white box with black text so it reads as pure
  black and white in the final image; the 4-level fallback clears its area
  from the overlay masks.
- A `.jpg`/`.jpeg`/`.png` entry that cannot be decoded (corrupt or mislabelled
  file, unsupported JPEG variant) shows a page error; the rest of the book
  stays readable.

## Phase 2: the device path and the waveform lab

The X4 runs the two passes without any SDK change. Both are factory-mode
refreshes staged in the facade's Overlay mode; Absolute mode is never entered.
Facts from the pinned SDK (`freeink-sdk/libs/display/FreeInkDisplay/src/`):

- Absolute mode cannot carry the sequence: `displayGrayBuffer()` rejects a
  custom LUT there, and ending an Absolute pass goes through
  `cancelGrayscalePass()`, which sets the facade's private `_grayPassFailed`;
  `displayGrayBuffer()` then returns early until a B/W refresh or a grayscale
  base clears the flag (`FreeInkDisplay.cpp`, `displayGrayBuffer` and
  `cancelGrayscalePass`). A pass 1 after an Absolute pass 0 would upload its
  planes and never run a waveform.
- `beginGray16()` has no display side effect: it only arms the HAL's pass
  state. It returns false without a fine LUT, on any controller other than the
  SSD1677 (the X3's UC8253 and the UC8279 advertise the same capabilities but
  ignore custom LUTs), and right after an Absolute-mode pass (a BMP image, an
  EPUB image page or the sleep cover) until the next B/W refresh, so callers
  take the 4-level path. An Absolute pass that was started but never ran (a
  failed image decode) keeps the refusal until the next grayscale base, because
  the driver would otherwise complement the coarse planes a second time.
- Pass 0: planes 2 and 3 go up through `writeGrayscalePlaneStrip` from a
  240-byte stack chunk, complemented by the HAL, because the factory LUT
  selects black at raw 11 and white at raw 00 while Overlay-mode writes are
  raw. Then `displayGrayBuffer(turnOff, nullptr, factoryMode = true)` runs
  the board's own factory 4-level LUT (`lut_factory_quality` on the X4, 50
  frames, the pass used for sleep covers) with the 0xCC activation.
- Pass 1: planes 0 and 1 go up raw, then `displayGrayBuffer(turnOff, fineLut,
  true)` runs the fine LUT the same way. No base refresh happens between the
  passes because `displayGrayscaleBase(Overlay)` is never called.
- `endGray16(bw)` is `cleanupGrayscaleBuffers(bw)`: the B/W baseline goes to
  RED RAM. Both factory passes set the driver's `_needsGrayClear`, so the next
  Fast refresh is promoted to a Half/Full clean (one flash on the next page
  turn, as after a sleep cover).
- Cost of the complement: 240 strip writes per coarse plane on the X4 (two
  rows per strip), an estimated 35-40 ms per plane on the 10 MHz display SPI,
  so about 75 ms per page. The lab logs upload and refresh times separately;
  if the overhead matters, phase 3 can replace it with a copy of the factory
  LUT whose VS rows are swapped (0 with 3, 1 with 2) so pass 0 also runs on
  raw planes (per board: Metalio ships its own factory LUT).

Only the fine LUT is new. It lives on the SD card so it can be tuned without
reflashing; the firmware ships no default for readers until phase 3 bakes in
a tuned one (the lab has a built-in starting point).

### Fine LUT format

112 bytes, the SSD1677 layout the SDK already uses: five VS rows of 10 bytes
(10 timing groups; each byte holds the four phases A-D of one group, 2 bits
per phase: `00` VSS, `01` VSH1, `10` VSL, `11` VSH2), then 10 groups of
`TPA, TPB, TPC, TPD, RP`, then 5 frame-rate bytes (two 4-bit codes each),
then `VGH, VSH1, VSH2, VSL, VCOM` and two reserved bytes. From the factory
LUT's rows, VSH1 drives toward black and VSL toward white.

VS rows are in controller order: row index `= (BW-RAM bit << 1) | RED-RAM
bit`. Pass 1 writes the LSB plane to BW RAM and the MSB plane to RED RAM, so
the public value `v = (MSB << 1) | LSB` lands on row 3 for v = 3 (no change),
row 1 for v = 2, row 2 for v = 1 and row 0 for v = 0.

### Files on the card

`/.crosspoint/gray16/fine.cfg` (text) or `/.crosspoint/gray16/fine.lut` (raw
112 bytes; wins when both exist). The config is turned into a LUT by
`src/util/Gray16Lut.cpp`:

```
# extra frames after the coarse pass for the classes needing 1, 2, 3 steps
darken = 2 4 7
fr = 2          # frame-rate code 0-15, all groups (the factory pass uses 2)
vgh = 0x17      # voltage tail; defaults are the X4 factory values
vsh1 = 0x41
vsh2 = 0xA8
vsl = 0x32
vcom = 0x30
```

`darken` gives VSH1 frames (-31..31) for the classes v = 2, 1, 0; a negative
value uses VSL frames instead (lighten). The builder sorts the magnitudes and
spreads them over up to three timing groups (phase A only, RP 0), so a class
drives in every group up to its own length. All other phases are VSS.

Safety: the five voltage codes must stay inside the SSD1677's documented
ranges (VGH `0x03..0x17`, VSH1/VSH2 `0x23..0x50` or `0x8E..0xCE`, VSL
`0x0A..0x3A`, VCOM `0x08..0x78`); a raw `fine.lut` must also pass those
checks and may not exceed 255 frames in total. A file that fails, or a config
over 512 bytes, is rejected with an `ERR` line (origin `G16`) in the boot log,
the device stays on the 4-level path, and the lab footer reads "built-in
default".

Opt-in: at boot the firmware loads the file if present and gives the LUT to
`HalDisplay::setGray16FineLut`; from then on `supportsGray16()` is true on
SSD1677 boards (X4, the SSD1677 X4 Pro batch, Sticky, Picco, Metalio) and the
CBZ reader uses the two-pass path. Until a tuned waveform exists the result is
experimental. Without a file the device stays on the 4-level path, and other
controllers never report support. The default voltage tail is the X4's;
Metalio boards tune VSH1 and VCOM differently in their factory LUT, so set
`vsh1` and `vcom` from `lut_factory_quality_metalio` there.

### The lab screen

Settings > General > "16-Level Lab". It draws a test card through the two-pass
path: 16 vertical bands (levels 0..15, labelled) in the upper part and a 4 x 4
grid in the lower part (rows = coarse level, columns = fine class) so the
size of the fine steps can be compared across coarse levels. Confirm reloads
`fine.cfg` / `fine.lut` and redraws (a built-in default is used when no file
exists, so the lab works on a fresh card); Left/Right switch between the full
card and a coarse-only reference (pass 0 alone); Back exits and reloads the
card's LUT (or clears it when no file exists), so readers use 16 levels only
when the file exists. The serial log (`INF`) prints the LUT source, the config
values, each pass's upload and refresh time and the free heap.

Iteration loop: edit `fine.cfg` on a computer and get it into
`/.crosspoint/gray16/`: either move the card, or use the web file manager
with "Show Hidden Files" turned on in Settings (the lab creates the folder
the first time it opens; delete the previous `fine.cfg` before uploading,
because the uploader refuses an existing name). Open the lab, press Confirm,
photograph the card. Aim for 16 bands that are monotonic and evenly spaced.
If the two middle fine steps come out swapped inside every coarse band, the
row mapping is the culprit: swap the first two entries of `ROW_OF_CLASS` in
`src/util/Gray16Lut.cpp` rather than re-tuning. If the three steps have
different sizes depending on the coarse level (a physical property of the
ink; pass 1 cannot tell the coarse levels apart), phase 3 adds a measured tone
curve in the Gray16 writer so the dither targets the levels the panel actually
produces.

## Costs and limits

- Cache: 192 KB per page, never evicted while the book exists. A 200-page
  volume uses about 38 MB of SD space.
- First view: extract the entry, decode it to an 8-bit gray BMP in the cache
  directory, then read that BMP back row by row, dither and write the planes:
  three SD passes over the page plus the decode. Not measured yet; a large
  scan (a 2000 px wide JPEG) is expected to take 5-12 s on the X4. Candidates
  for a later phase if that is too slow: stream the decoder's rows straight into the
  dither through a `Print` adapter (saves the BMP round trip), use JPEGDEC's
  1/2..1/8 scaling for oversized pages, and decode stored (uncompressed) ZIP
  entries in place instead of extracting them.
- Later views, 16-level: five 48 KB reads (four planes, then plane 3 again as
  the B/W baseline) plus two refreshes. 4-level fallback: two plane reads
  (planes 3 and 2), one B/W base refresh and one grayscale refresh.
- Display time on the device is two factory passes: 50 frames for the coarse
  pass plus the fine pulses; expected about 1 s per page (to be measured in
  the lab).
- Images are scaled by the existing converters' nearest/linear scaling; no
  rotation of landscape pages onto a portrait screen (they are letterboxed).

## Tests

- `test/gray16`: dither range and determinism, writer/reader round trip on a
  small synthetic panel for all four orientations, letterbox padding, header
  validation.
- `test/cbz`: entry filtering and natural sort, page enumeration from a
  synthetic ZIP, cache validation and regeneration of stale page files,
  progress percentage from a page index and count.
- `test/sim_display`: the simulator's absolute-plane and two-pass luminance
  model (`g * 17`).
- `test/gray16lut`: the `fine.cfg` parser (keys, ranges, voltage codes,
  errors), the LUT builder's byte layout, `validateLut`, and the loader's
  precedence and fallbacks against an in-memory card.
- `simulator/tests/gray16_lab_smoke.sh` (run by the `native-smoke` CI job):
  opens the lab, checks the exact 16 luminances on the bands and the grid,
  the coarse-only reference, a Confirm reload, Back, and the unsupported
  screen with `CROSSPOINT_SIM_NO_GRAY16=1`.
- `simulator/tests/cbz_smoke.sh` (run by the `native-smoke` CI job): builds
  a fixture `.cbz` with `cbz_fixture.py` (16-step gradient, a small page that
  gets letterboxed, real JPEG/PNG pages and junk entries), opens it in the
  native simulator and checks that the presented frame holds exactly the 16
  luminances `g * 17`, the letterbox, the page order and, with
  `CROSSPOINT_SIM_NO_GRAY16=1` (native build only), the 4-level fallback
  mapping `[0, 85, 170, 255]`. The browser build is compile-only in CI; the
  browser path was checked by hand with a Playwright script during
  development (not in the repository).
