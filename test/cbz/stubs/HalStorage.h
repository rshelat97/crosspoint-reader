#pragma once

// In-memory stand-in for HalStorage/HalFile: a map of paths to byte vectors
// plus a set of directories, enough for ZipFile, Bitmap and Cbz to run on the
// host. Not thread-safe; tests run on one thread.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t data) = 0;
  virtual size_t write(const uint8_t* data, size_t size) {
    size_t written = 0;
    while (written < size && write(data[written]) == 1) written++;
    return written;
  }
};

class HalFile : public Print {
  friend class HalStorage;

 public:
  size_t size() { return data ? data->size() : 0; }
  size_t position() const { return pos; }
  int available() const { return data ? static_cast<int>(data->size() - pos) : 0; }
  bool seek(size_t offset) { return seekSet(offset); }
  bool seekSet(size_t offset) {
    if (!data || offset > data->size()) return false;
    pos = offset;
    return true;
  }
  bool seekCur(int64_t offset) {
    const int64_t target = static_cast<int64_t>(pos) + offset;
    if (target < 0) return false;
    return seekSet(static_cast<size_t>(target));
  }
  int read(void* dst, size_t len) {
    if (!data) return -1;
    const size_t remaining = data->size() - pos;
    const size_t count = len < remaining ? len : remaining;
    std::memcpy(dst, data->data() + pos, count);
    pos += count;
    return static_cast<int>(count);
  }
  int read() {
    uint8_t byte;
    return read(&byte, 1) == 1 ? byte : -1;
  }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* src, size_t len) override {
    if (!data || !writable) return 0;
    if (pos + len > data->size()) data->resize(pos + len);
    std::memcpy(data->data() + pos, src, len);
    pos += len;
    return len;
  }
  void flush() {}
  bool close() {
    data = nullptr;
    pos = 0;
    return true;
  }
  bool isOpen() const { return data != nullptr; }
  explicit operator bool() const { return isOpen(); }

 private:
  std::vector<uint8_t>* data = nullptr;
  size_t pos = 0;
  bool writable = false;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  void reset() {
    files.clear();
    dirs.clear();
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    file.close();
    const auto it = files.find(path);
    if (it == files.end()) return false;
    file.data = &it->second;
    file.pos = 0;
    file.writable = false;
    return true;
  }
  bool openFileForRead(const char* module, const char* path, HalFile& file) {
    return openFileForRead(module, std::string(path), file);
  }

  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    file.close();
    auto& bytes = files[path];
    bytes.clear();
    file.data = &bytes;
    file.pos = 0;
    file.writable = true;
    return true;
  }
  bool openFileForWrite(const char* module, const char* path, HalFile& file) {
    return openFileForWrite(module, std::string(path), file);
  }

  bool exists(const char* path) { return files.count(path) > 0 || dirs.count(path) > 0; }
  bool mkdir(const char* path, bool = true) {
    dirs.insert(path);
    return true;
  }
  bool remove(const char* path) { return files.erase(path) > 0; }
  bool rename(const char* oldPath, const char* newPath) {
    const auto it = files.find(oldPath);
    if (it == files.end() || files.count(newPath) > 0) return false;
    files[newPath] = std::move(it->second);
    files.erase(it);
    return true;
  }
  bool replaceFile(const char* tmpPath, const char* path) {
    files.erase(path);
    return rename(tmpPath, path);
  }
  bool removeDir(const char* path) {
    const std::string prefix = std::string(path) + "/";
    for (auto it = files.begin(); it != files.end();) {
      it = it->first.compare(0, prefix.size(), prefix) == 0 ? files.erase(it) : std::next(it);
    }
    dirs.erase(path);
    return true;
  }

  void setFile(const std::string& path, std::vector<uint8_t> bytes) { files[path] = std::move(bytes); }
  const std::vector<uint8_t>* fileBytes(const std::string& path) const {
    const auto it = files.find(path);
    return it == files.end() ? nullptr : &it->second;
  }
  // Paths of stored files that start with `prefix`.
  std::vector<std::string> filesUnder(const std::string& prefix) const {
    std::vector<std::string> out;
    for (const auto& entry : files) {
      if (entry.first.compare(0, prefix.size(), prefix) == 0) out.push_back(entry.first);
    }
    return out;
  }

 private:
  std::map<std::string, std::vector<uint8_t>> files;
  std::set<std::string> dirs;
};

#define Storage HalStorage::getInstance()
