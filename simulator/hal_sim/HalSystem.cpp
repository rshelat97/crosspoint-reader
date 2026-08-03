// Simulator implementation of HalSystem: no panic machinery on the host.
#include <HalSystem.h>

namespace HalSystem {
void begin() {}
void checkPanic() {}
void clearPanic() {}
std::string getPanicInfo(bool) { return {}; }
bool isRebootFromPanic() { return false; }
}  // namespace HalSystem
