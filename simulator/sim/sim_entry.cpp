// Simulator entry point. Drives the REAL firmware setup()/loop() from
// src/main.cpp — the firmware code is identical to what runs on device; only
// the HAL beneath it is simulated.
//
// Native: headless driver with a scriptable smoke-test mode (used by CI and
// local verification).
// Emscripten: exports a C API for the web frontend (button/touch injection,
// RGBA framebuffer access) and persists /simfs to IndexedDB.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "sim_display.h"
#include "sim_input.h"

extern void setup();
extern void loop();

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

extern "C" {

EMSCRIPTEN_KEEPALIVE void sim_button(int btnIndex, int down) {
  siminput::injectButton(static_cast<uint8_t>(btnIndex), down != 0);
}
EMSCRIPTEN_KEEPALIVE void sim_touch(int type, float nx, float ny) {
  if (type == 0)
    siminput::injectTouchDown(nx, ny);
  else if (type == 1)
    siminput::injectTouchMove(nx, ny);
  else
    siminput::injectTouchUp(nx, ny);
}
EMSCRIPTEN_KEEPALIVE const uint8_t* sim_fb() { return simdisp::rgba(); }
EMSCRIPTEN_KEEPALIVE int sim_fb_width() { return simdisp::PANEL_W; }
EMSCRIPTEN_KEEPALIVE int sim_fb_height() { return simdisp::PANEL_H; }
EMSCRIPTEN_KEEPALIVE uint32_t sim_fb_frame() { return simdisp::frameCounter(); }
EMSCRIPTEN_KEEPALIVE int sim_refresh_mode() { return simdisp::lastRefreshMode(); }

EMSCRIPTEN_KEEPALIVE void sim_save_fs() {
  EM_ASM({
    FS.syncfs(
        false, function(err) {
          if (err) console.warn('simfs persist failed', err);
        });
  });
}

}  // extern "C"

namespace {

enum class BootState { MountFs, WaitFs, Boot, Run };
BootState bootState = BootState::MountFs;
bool fsReady = false;

extern "C" EMSCRIPTEN_KEEPALIVE void sim_fs_ready() { fsReady = true; }

void tick() {
  switch (bootState) {
    case BootState::MountFs:
      EM_ASM({
        FS.mkdir('/simfs');
        FS.mount(IDBFS, {}, '/simfs');
        FS.syncfs(
            true, function(err) {
              if (err) console.warn('simfs load failed', err);
              Module._sim_fs_ready();
            });
      });
      bootState = BootState::WaitFs;
      break;
    case BootState::WaitFs:
      if (fsReady) bootState = BootState::Boot;
      break;
    case BootState::Boot:
      setup();
      bootState = BootState::Run;
      break;
    case BootState::Run:
      loop();
      break;
  }
}

}  // namespace

int main() {
  emscripten_set_main_loop(tick, 0, 0);
  return 0;
}

#else  // native ---------------------------------------------------------------

extern "C" {
int sim_http_request(const char* method, const char* uriWithQuery, const uint8_t* body, int bodyLen,
                     const char* headerBlock);
const uint8_t* sim_http_response_body();
int sim_http_response_body_len();
}

namespace {

struct KeyScript {
  int btn;
  unsigned long downFrame;
  unsigned long upFrame;
};

}  // namespace

int main(int argc, char** argv) {
  unsigned long frames = 0;  // 0 = run forever
  const char* dumpPath = nullptr;
  std::vector<KeyScript> script;
  std::vector<std::string> httpRequests;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
      frames = strtoul(argv[++i], nullptr, 10);
    } else if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
      dumpPath = argv[++i];
    } else if (strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
      // --key <btnIndex>:<downFrame>:<upFrame>
      KeyScript k{};
      if (sscanf(argv[++i], "%d:%lu:%lu", &k.btn, &k.downFrame, &k.upFrame) == 3) script.push_back(k);
    } else if (strcmp(argv[i], "--http") == 0 && i + 1 < argc) {
      // --http "GET /api/status" — sent to the firmware web server after the
      // frame loop (requires the script to have started File Transfer mode).
      httpRequests.emplace_back(argv[++i]);
    } else {
      fprintf(stderr, "usage: %s [--frames N] [--dump out.ppm] [--key btn:down:up]... [--http \"GET /path\"]...\n",
              argv[0]);
      return 2;
    }
  }

  setup();
  for (unsigned long f = 0; frames == 0 || f < frames; f++) {
    for (const auto& k : script) {
      if (f == k.downFrame) siminput::injectButton(static_cast<uint8_t>(k.btn), true);
      if (f == k.upFrame) siminput::injectButton(static_cast<uint8_t>(k.btn), false);
    }
    loop();
  }
  if (dumpPath) {
    if (!simdisp::dumpPpm(dumpPath)) {
      fprintf(stderr, "failed to write %s\n", dumpPath);
      return 1;
    }
    printf("[SIM] wrote %s (frame %u)\n", dumpPath, simdisp::frameCounter());
  }
  int failures = 0;
  for (const auto& req : httpRequests) {
    const size_t space = req.find(' ');
    const std::string method = space == std::string::npos ? "GET" : req.substr(0, space);
    const std::string path = space == std::string::npos ? req : req.substr(space + 1);
    const int code = sim_http_request(method.c_str(), path.c_str(), nullptr, 0, "");
    printf("[SIM] HTTP %s %s -> %d (%d bytes)\n", method.c_str(), path.c_str(), code, sim_http_response_body_len());
    if (code < 200 || code >= 400) failures++;
  }
  return failures == 0 ? 0 : 1;
}

#endif
