// Simulator stubs for peripheral drivers the X4 does not have. The headers
// are the real SDK headers; with the X4 board profile, present() is already
// false (no FREEINK_CAP_FRONTLIGHT), so these bodies only need to be inert.
#include <FrontlightManager.h>

void FrontlightManager::begin() {}
void FrontlightManager::setBrightness(uint8_t) {}
void FrontlightManager::setBrightnessLevel(uint8_t) {}
void FrontlightManager::off() {}
void FrontlightManager::on() {}
void FrontlightManager::setColorTemperature(uint8_t) {}
