# CrossPoint Web/Host Simulator

Runs the **real firmware** — `src/main.cpp`, all activities, the EPUB engine,
`GfxRenderer`, UITheme, i18n — on a desktop host or in the browser. Only the
hardware layer is replaced. This is the same code that ships to the device,
compiled for a different target, so reader layout, pagination, caching and UI
behaviour are faithful.

## Try it

The GitHub Actions workflow (`.github/workflows/simulator.yml`) builds the
WASM bundle on every push and deploys it to GitHub Pages.

One-time setup: repository **Settings → Pages → Build and deployment →
Source: "GitHub Actions"**. To deploy from a non-default branch, also allow it
under **Settings → Environments → github-pages → Deployment branches**.

In the app: drag an `.epub` onto the screen to add it to the library, click
the screen to tap, drag for swipes, use the on-screen buttons or the keyboard
(arrows/PgUp/PgDn navigate, Enter confirms, Esc goes back, hold `P` to sleep).
The simulated SD card lives in browser IndexedDB, so books, settings and
reading progress survive reloads. The device mock mirrors the physical X4:
page keys and power on the right edge, and the two front paddles (Back/Confirm
and Left/Right) below the screen.

### File Transfer (the real device web UI)

File Transfer works end to end: on the simulated device choose **Home → File
Transfer → any mode** (the Wi-Fi radio is faked — scans find a couple of
pretend networks and joining always succeeds). That starts the REAL
`CrossPointWebServer` inside the wasm module. Click **"Device web UI"** in the
toolbar (or browse to `device/` under the simulator URL) and you get the
actual firmware-served web interface — Home/status, the File Manager (upload,
download, rename, move, delete), Settings and Fonts pages — operating on the
simulated SD card.

Under the hood a service worker forwards `device/...` fetches (and the pages'
server-absolute `/api/...` calls) into the firmware's request handlers — no
sockets involved, so it works on plain static hosting. WebSocket upload can't
run in a browser page; the Files page automatically falls back to its HTTP
upload path. WebDAV handlers are compiled in but external WebDAV clients
can't reach the simulator.

## Build natively (Linux/macOS)

```bash
cmake -S simulator -B build/sim-native -G Ninja
cmake --build build/sim-native

# Interactive-less smoke run: boot firmware, run 300 frames, dump the screen
./build/sim-native/crosspoint_sim --frames 300 --dump home.ppm

# Scripted input: press button 1 (Confirm) down on frame 10, up on frame 12
CROSSPOINT_SIM_FS=./simfs ./build/sim-native/crosspoint_sim \
    --frames 600 --dump out.ppm --key 1:10:12
```

The simulated SD card is a host directory (`$CROSSPOINT_SIM_FS`, default
`./simfs`). Drop EPUBs into `simfs/books/` and they appear in the file
browser; the `.crosspoint` cache is created there too, exactly as on device.

Sandboxed/offline configure: add `-DSIM_FETCH_JPEGDEC=OFF
-DSIM_FETCH_ARDUINOJSON=OFF` to skip the FetchContent downloads and fall back
to bundled stubs (JPEG covers then fail gracefully; JSON uses a mini
implementation).

## Build for the browser

```bash
emcmake cmake -S simulator -B build/sim-wasm -G Ninja
cmake --build build/sim-wasm
# build/sim-wasm now contains index.html + app.js + crosspoint_sim.{js,wasm}
python3 -m http.server -d build/sim-wasm 8080   # open http://localhost:8080
```

Verified with Emscripten 3.1.6 and 3.1.64. No pthreads are used, so the page
needs no COOP/COEP headers and works on plain GitHub Pages hosting.

## How it works

```
simulator/
├── CMakeLists.txt     # compiles src/ + lib/ + freeink-sdk UI against the shims
├── shim/include/      # header shims: Arduino.h, WString/Print/Stream,
│                      #   freertos/*, EInkDisplay, BoardConfig, mbedtls, ...
├── shim/shim.cpp      # millis/delay, ESP heap stats, FreeRTOS single-thread
│                      #   collapse (tasks registered, mutexes are counters)
├── hal_sim/           # lib/hal replacements: display presents to sim_display,
│                      #   GPIO reads sim_input, storage maps to POSIX files
├── sim/               # simulator core: framebuffer→RGBA presenter, input
│                      #   event queue with firmware-accurate edge semantics,
│                      #   entry point (native driver / Emscripten exports)
├── stubs/             # network activities render a "not available" notice;
│                      #   HTTP/OTA/KOReader clients return error codes;
│                      #   JPEGDEC/PNGdec/QRCode/ArduinoJson fallbacks
└── web/               # static frontend (canvas blit, buttons, drag & drop)
```

Key design points:

- **Single firmware `#ifdef`**: the only firmware source touched is
  `ActivityManager`, where `CROSSPOINT_SIMULATOR` replaces the render-task
  `xTaskNotify` with a synchronous render call. Everything else compiles
  unmodified.
- **FreeRTOS collapses to one thread**: `xTaskCreate` registers the task but
  the render loop is driven synchronously, so no pthreads (and no special
  browser headers) are needed. Semaphores are depth counters.
- **Display semantics match the panel**: 1-bpp frames present as B/W; 2-bpp
  grayscale planes composite over the previously displayed frame exactly like
  the real waveforms (value 0 = untouched, 1–3 darken).
- **Input contract mirrors `InputManager`**: frame-scoped pressed/released
  edges, tap slop, swipe thresholds and hold timing are recomputed per
  `update()` just like the freeink SDK, and touch coordinates are fed
  panel-native so the firmware's own `tapToLogical()` handles orientation.
- **Storage maps 1:1**: firmware paths land under the sim root, so the
  `.crosspoint` cache format, book hashes and progress files behave exactly
  as on an SD card (persisted to IndexedDB in the browser).

## What is (deliberately) not simulated

- Real network access: the Wi-Fi radio is faked (fake scan results, instant
  joins), which is enough for the REAL Wi-Fi selection and File Transfer
  activities plus the web server to run. True network clients — OPDS
  browsing, Calibre wireless, KOReader sync, OTA download — show a notice and
  back out or return error codes.
- OTA flashing, battery drain (fixed 87%), USB, and the tilt sensor.
- E-ink refresh artifacts: a FULL refresh is visualised with a brief flash.
