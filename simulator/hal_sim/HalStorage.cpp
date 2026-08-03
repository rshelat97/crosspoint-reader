// Simulator implementation of HalStorage/HalFile: the SD card becomes a host
// directory tree (native: $CROSSPOINT_SIM_FS or ./simfs; WASM: /simfs on
// MEMFS/IDBFS). Firmware paths map 1:1 under that root, so the .crosspoint
// cache, bookmarks, dictionaries and books all work unchanged.
#include <HalStorage.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

HalStorage HalStorage::instance;

namespace {

std::string simRoot() {
#ifdef __EMSCRIPTEN__
  return "/simfs";
#else
  const char* env = getenv("CROSSPOINT_SIM_FS");
  return env && *env ? env : "./simfs";
#endif
}

std::string mapPath(const char* fwPath) {
  std::string p = simRoot();
  if (!fwPath || !*fwPath) return p;
  if (fwPath[0] != '/') p += '/';
  p += fwPath;
  // Collapse a trailing slash (SdFat tolerates it; opendir mostly does too,
  // but keep paths canonical for stat/rename).
  while (p.size() > 1 && p.back() == '/') p.pop_back();
  return p;
}

bool isDir(const std::string& hostPath) {
  struct stat st{};
  return stat(hostPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool makeDirRecursive(const std::string& hostPath) {
  if (hostPath.empty() || isDir(hostPath)) return true;
  std::string partial;
  size_t pos = 0;
  while (pos <= hostPath.size()) {
    const size_t next = hostPath.find('/', pos);
    partial = next == std::string::npos ? hostPath : hostPath.substr(0, next);
    if (!partial.empty() && partial != "." && !isDir(partial)) {
      if (mkdir(partial.c_str(), 0755) != 0 && !isDir(partial)) return false;
    }
    if (next == std::string::npos) break;
    pos = next + 1;
  }
  return isDir(hostPath);
}

bool removeRecursive(const std::string& hostPath) {
  if (isDir(hostPath)) {
    DIR* d = opendir(hostPath.c_str());
    if (!d) return false;
    while (struct dirent* e = readdir(d)) {
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
      if (!removeRecursive(hostPath + "/" + e->d_name)) {
        closedir(d);
        return false;
      }
    }
    closedir(d);
    return rmdir(hostPath.c_str()) == 0;
  }
  return ::remove(hostPath.c_str()) == 0;
}

}  // namespace

// --- HalFile::Impl -----------------------------------------------------------

class HalFile::Impl {
 public:
  FILE* f = nullptr;
  DIR* d = nullptr;
  std::string hostPath;  // full host path of this file/dir
  bool writable = false;

  ~Impl() { closeAll(); }

  bool closeAll() {
    bool ok = true;
    if (f) {
      ok = fclose(f) == 0;
      f = nullptr;
    }
    if (d) {
      closedir(d);
      d = nullptr;
    }
    return ok;
  }

  bool open() const { return f != nullptr || d != nullptr; }

  size_t sizeBytes() const {
    struct stat st{};
    if (stat(hostPath.c_str(), &st) != 0) return 0;
    return static_cast<size_t>(st.st_size);
  }
};

// --- HalFile -----------------------------------------------------------------

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

void HalFile::flush() {
  if (impl && impl->f) fflush(impl->f);
}

size_t HalFile::getName(char* name, size_t len) {
  if (!impl || len == 0) return 0;
  const auto slash = impl->hostPath.rfind('/');
  const std::string base = slash == std::string::npos ? impl->hostPath : impl->hostPath.substr(slash + 1);
  const size_t n = base.size() < len - 1 ? base.size() : len - 1;
  memcpy(name, base.c_str(), n);
  name[n] = '\0';
  return n;
}

size_t HalFile::size() { return impl ? impl->sizeBytes() : 0; }
size_t HalFile::fileSize() { return size(); }
uint64_t HalFile::fileSize64() { return size(); }

bool HalFile::seek(size_t pos) { return impl && impl->f && fseek(impl->f, static_cast<long>(pos), SEEK_SET) == 0; }
bool HalFile::seek64(uint64_t pos) { return seek(static_cast<size_t>(pos)); }
bool HalFile::seekCur(int64_t offset) {
  return impl && impl->f && fseek(impl->f, static_cast<long>(offset), SEEK_CUR) == 0;
}
bool HalFile::seekSet(size_t offset) { return seek(offset); }

int HalFile::available() const {
  if (!impl || !impl->f) return 0;
  const long pos = ftell(impl->f);
  const size_t total = impl->sizeBytes();
  if (pos < 0) return 0;
  return static_cast<size_t>(pos) >= total ? 0 : static_cast<int>(total - static_cast<size_t>(pos));
}

size_t HalFile::position() const {
  if (!impl || !impl->f) return 0;
  const long pos = ftell(impl->f);
  return pos < 0 ? 0 : static_cast<size_t>(pos);
}

int HalFile::read(void* buf, size_t count) {
  if (!impl || !impl->f) return -1;
  return static_cast<int>(fread(buf, 1, count, impl->f));
}

int HalFile::read() {
  if (!impl || !impl->f) return -1;
  return fgetc(impl->f);
}

size_t HalFile::write(const void* buf, size_t count) {
  if (!impl || !impl->f || !impl->writable) return 0;
  return fwrite(buf, 1, count, impl->f);
}

size_t HalFile::write(uint8_t b) { return write(&b, 1); }

bool HalFile::rename(const char* newPath) {
  if (!impl) return false;
  const std::string dst = mapPath(newPath);
  impl->closeAll();
  if (::rename(impl->hostPath.c_str(), dst.c_str()) != 0) return false;
  impl->hostPath = dst;
  return true;
}

bool HalFile::isDirectory() const { return impl && impl->d != nullptr; }

void HalFile::rewindDirectory() {
  if (impl && impl->d) rewinddir(impl->d);
}

bool HalFile::close() {
  if (!impl) return false;
  const bool ok = impl->closeAll();
  return ok;
}

HalFile HalFile::openNextFile() {
  auto next = std::make_unique<Impl>();
  if (impl && impl->d) {
    while (struct dirent* e = readdir(impl->d)) {
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
      const std::string child = impl->hostPath + "/" + e->d_name;
      if (isDir(child)) {
        next->d = opendir(child.c_str());
        if (!next->d) continue;
      } else {
        next->f = fopen(child.c_str(), "rb");
        if (!next->f) continue;
      }
      next->hostPath = child;
      break;
    }
  }
  // Always wrap an Impl (never a null impl) — operator bool is the only
  // valid end-of-iteration test, mirroring the firmware implementation.
  return HalFile(std::move(next));
}

bool HalFile::isOpen() const { return impl && impl->open(); }
HalFile::operator bool() const { return isOpen(); }

// --- HalStorage --------------------------------------------------------------

HalStorage::HalStorage() { storageMutex = xSemaphoreCreateRecursiveMutex(); }

bool HalStorage::begin() {
  initialized = makeDirRecursive(simRoot());
  return initialized;
}

bool HalStorage::ready() const { return initialized; }

std::vector<String> HalStorage::listFiles(const char*, int) { return {}; }

String HalStorage::readFile(const char* path) {
  HalFile f;
  if (!openFileForRead("SIMFS", path, f)) return String();
  std::string out;
  out.resize(f.size());
  const int n = f.read(out.data(), out.size());
  out.resize(n > 0 ? static_cast<size_t>(n) : 0);
  return String(out);
}

bool HalStorage::readFileToStream(const char*, Print&, size_t) { return false; }
size_t HalStorage::readFileToBuffer(const char*, char*, size_t, size_t) { return 0; }

bool HalStorage::writeFile(const char* path, const String& content) {
  HalFile f;
  if (!openFileForWrite("SIMFS", path, f)) return false;
  return f.write(content.c_str(), content.length()) == content.length();
}

bool HalStorage::ensureDirectoryExists(const char* path) { return makeDirRecursive(mapPath(path)); }

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  auto impl = std::make_unique<HalFile::Impl>();
  impl->hostPath = mapPath(path);
  if (isDir(impl->hostPath)) {
    impl->d = opendir(impl->hostPath.c_str());
  } else if ((oflag & (O_WRONLY | O_RDWR | O_CREAT)) != 0) {
    const bool truncate = (oflag & O_TRUNC) != 0;
    const bool mustExist = (oflag & O_CREAT) == 0;
    struct stat st{};
    const bool exists = stat(impl->hostPath.c_str(), &st) == 0;
    if (mustExist && !exists) return HalFile(std::move(impl));
    impl->f = fopen(impl->hostPath.c_str(), truncate || !exists ? "w+b" : "r+b");
    impl->writable = impl->f != nullptr;
  } else {
    impl->f = fopen(impl->hostPath.c_str(), "rb");
  }
  return HalFile(std::move(impl));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  const std::string hostPath = mapPath(path);
  if (pFlag) return makeDirRecursive(hostPath);
  return ::mkdir(hostPath.c_str(), 0755) == 0 || isDir(hostPath);
}

bool HalStorage::exists(const char* path) {
  struct stat st{};
  return stat(mapPath(path).c_str(), &st) == 0;
}

bool HalStorage::remove(const char* path) { return ::remove(mapPath(path).c_str()) == 0; }

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  return ::rename(mapPath(oldPath).c_str(), mapPath(newPath).c_str()) == 0;
}

bool HalStorage::rmdir(const char* path) { return ::rmdir(mapPath(path).c_str()) == 0; }

bool HalStorage::removeDir(const char* path) { return removeRecursive(mapPath(path)); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  file = open(path, O_RDONLY);
  if (!file || file.isDirectory()) {
    fprintf(stderr, "[SIMFS] %s: failed to open for read: %s\n", moduleName, path);
    return false;
  }
  return true;
}
bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}
bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  file = open(path, static_cast<oflag_t>(O_WRITE | O_CREAT | O_TRUNC));
  if (!file) {
    fprintf(stderr, "[SIMFS] %s: failed to open for write: %s\n", moduleName, path);
    return false;
  }
  return true;
}
bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}
bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}
