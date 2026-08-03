// Simulator implementation of HalPowerManager: no CPU scaling, fixed battery.
// Deep sleep ends the run — natively the process exits; in the browser the
// main loop stops and the frontend shows a "device is asleep" overlay
// (reloading the page is the wake-up reset).
#include <HalPowerManager.h>

#include <cstdio>
#include <cstdlib>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

HalPowerManager powerManager;

void HalPowerManager::begin() { modeMutex = xSemaphoreCreateMutex(); }

void HalPowerManager::setPowerSaving(bool) {}

void HalPowerManager::startDeepSleep(HalGPIO&) const {
#ifdef __EMSCRIPTEN__
  fprintf(stderr, "[SIM] deep sleep requested — halting main loop\n");
  EM_ASM({
    FS.syncfs(false, function(err){});
    if (Module.onDeviceSleep) Module.onDeviceSleep();
  });
  emscripten_cancel_main_loop();
  // On device this call never returns (wake is a chip reset). Unwind out of
  // the current tick so no firmware code runs past this point; the string
  // 'unwind' is recognised by Emscripten's main-loop exception handler.
  EM_ASM({ throw 'unwind'; });
  __builtin_unreachable();
#else
  fprintf(stderr, "[SIM] deep sleep requested — exiting simulator\n");
  std::exit(0);
#endif
}

uint16_t HalPowerManager::getBatteryPercentage() const { return 87; }

HalPowerManager::Lock::Lock() : valid(true) {}
HalPowerManager::Lock::~Lock() = default;
