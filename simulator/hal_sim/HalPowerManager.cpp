// Simulator implementation of HalPowerManager: no CPU scaling, fixed battery,
// deep sleep exits the process (the sim main loop intercepts sleep before
// this is ever reached in normal use).
#include <HalPowerManager.h>

#include <cstdio>
#include <cstdlib>

HalPowerManager powerManager;

void HalPowerManager::begin() { modeMutex = xSemaphoreCreateMutex(); }

void HalPowerManager::setPowerSaving(bool) {}

void HalPowerManager::startDeepSleep(HalGPIO&) const {
  fprintf(stderr, "[SIM] deep sleep requested — exiting simulator\n");
  std::exit(0);
}

uint16_t HalPowerManager::getBatteryPercentage() const { return 87; }

HalPowerManager::Lock::Lock() : valid(true) {}
HalPowerManager::Lock::~Lock() = default;
