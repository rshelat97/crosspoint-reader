// Simulator stubs for the network client classes. Headers are the real
// firmware headers; every operation reports a network/hardware failure so
// callers take their offline error paths.
#include <KOReaderSyncClient.h>

#include "network/FirmwareFlasher.h"
#include "network/HttpDownloader.h"
#include "network/OtaBootSwitch.h"
#include "network/OtaUpdater.h"

// --- HttpDownloader ----------------------------------------------------------
bool HttpDownloader::fetchUrl(const std::string&, std::string&, const std::string&, const std::string&) {
  return false;
}
bool HttpDownloader::fetchUrl(const std::string&, Stream&, const std::string&, const std::string&) { return false; }
bool HttpDownloader::fetchUrl(const std::string&, const DataCallback&, const std::string&, const std::string&) {
  return false;
}
HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string&, const std::string&, ProgressCallback,
                                                             bool*, const std::string&, const std::string&) {
  return HTTP_ERROR;
}

// --- OtaUpdater --------------------------------------------------------------
bool OtaUpdater::isUpdateNewer() const { return false; }
const std::string& OtaUpdater::getLatestVersion() const {
  static const std::string none = "simulator";
  return none;
}
OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() { return HTTP_ERROR; }
OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback, void*) { return HTTP_ERROR; }

// --- KOReaderSyncClient ------------------------------------------------------
int KOReaderSyncClient::lastHttpCode = 0;
KOReaderSyncClient::Error KOReaderSyncClient::authenticate() { return NETWORK_ERROR; }
KOReaderSyncClient::Error KOReaderSyncClient::createUser() { return NETWORK_ERROR; }
KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string&, KOReaderProgress&) {
  return NETWORK_ERROR;
}
KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress&) { return NETWORK_ERROR; }
const char* KOReaderSyncClient::errorString(Error) { return "Not available in the simulator"; }

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
