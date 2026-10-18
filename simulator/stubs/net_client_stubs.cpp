// Simulator stubs for firmware update/flashing, which needs real flash
// partitions. (HTTP clients compile for real: with no network, SecureNet's
// connect() fails and callers take their offline error paths.) Headers are
// the real firmware headers; every operation reports failure.
#include "network/FirmwareFlasher.h"
#include "network/OtaBootSwitch.h"
#include "network/OtaUpdater.h"

// --- OtaUpdater --------------------------------------------------------------
bool OtaUpdater::isUpdateNewer() const { return false; }
const std::string& OtaUpdater::getLatestVersion() const {
  static const std::string none = "simulator";
  return none;
}
OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() { return HTTP_ERROR; }
OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback, void*) { return HTTP_ERROR; }

// --- firmware_flash / ota_boot ----------------------------------------------
namespace firmware_flash {
Result flashFromSdPath(const char*, ProgressCb, void*, bool) { return Result::NO_PARTITION; }
Result validateImageFile(const char*, size_t) { return Result::NO_PARTITION; }
const char* resultName(Result) { return "SIMULATOR"; }
}  // namespace firmware_flash

namespace ota_boot {
uint32_t computeSeqCrc(uint32_t) { return 0; }
bool switchTo(const esp_partition_t*) { return false; }
}  // namespace ota_boot
