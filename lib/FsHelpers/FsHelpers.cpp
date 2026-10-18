#include "FsHelpers.h"

#include <Utf8.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace FsHelpers {

namespace {
bool isHexDigit(const char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

uint8_t hexValue(const char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + (c - 'a'));
  return static_cast<uint8_t>(10 + (c - 'A'));
}
}  // namespace

std::string decodeUriEscapes(const std::string& path) {
  std::string decoded;
  decoded.reserve(path.size());

  for (size_t i = 0; i < path.size(); i++) {
    if (path[i] == '%' && i + 2 < path.size() && isHexDigit(path[i + 1]) && isHexDigit(path[i + 2])) {
      const uint8_t value = static_cast<uint8_t>((hexValue(path[i + 1]) << 4) | hexValue(path[i + 2]));
      decoded += static_cast<char>(value);
      i += 2;
      continue;
    }

    decoded += path[i];
  }

  return decoded;
}

std::string normalisePath(const std::string& path) {
  std::vector<std::string_view> components;
  components.reserve(8);  // Eight nested folders is more than we might expect

  size_t start = 0;
  for (size_t i = 0; i <= path.length(); ++i) {
    if (i == path.length() || path[i] == '/') {
      if (i > start) {
        std::string_view component(path.data() + start, i - start);
        if (component == "..") {
          if (!components.empty()) {
            components.pop_back();
          }
        } else {
          components.push_back(component);
        }
      }
      start = i + 1;
    }
  }

  if (components.empty()) {
    return "";
  }

  size_t total_len = 0;
  for (const auto& c : components) {
    total_len += c.length() + 1;
  }

  std::string result;
  result.reserve(total_len - 1);

  for (size_t i = 0; i < components.size(); ++i) {
    if (i > 0) {
      result += '/';
    }
    result.append(components[i].data(), components[i].length());
  }

  return result;
}

bool naturalLess(const std::string_view str1, const std::string_view str2) {
  // Naive natural sort: numeric-aware, case-insensitive
  size_t i = 0;
  size_t j = 0;

  // ctype functions require unsigned char values: passing a negative char (UTF-8
  // bytes above 0x7f with signed char) is undefined behavior
  const auto isDigit = [](const char c) { return isdigit(static_cast<unsigned char>(c)) != 0; };

  // Iterate while both strings have characters
  while (i < str1.size() && j < str2.size()) {
    // Check if both are at the start of a number
    if (isDigit(str1[i]) && isDigit(str2[j])) {
      // Skip leading zeros
      while (i < str1.size() && str1[i] == '0') i++;
      while (j < str2.size() && str2[j] == '0') j++;

      // Count digits to compare lengths first
      size_t len1 = 0, len2 = 0;
      while (i + len1 < str1.size() && isDigit(str1[i + len1])) len1++;
      while (j + len2 < str2.size() && isDigit(str2[j + len2])) len2++;

      // Different length so return smaller integer value
      if (len1 != len2) return len1 < len2;

      // Same length so compare digit by digit
      for (size_t k = 0; k < len1; k++) {
        if (str1[i + k] != str2[j + k]) return str1[i + k] < str2[j + k];
      }

      // Numbers equal so advance past them
      i += len1;
      j += len2;
    } else {
      // Regular case-insensitive character comparison
      const int c1 = tolower(static_cast<unsigned char>(str1[i]));
      const int c2 = tolower(static_cast<unsigned char>(str2[j]));
      if (c1 != c2) return c1 < c2;
      i++;
      j++;
    }
  }

  // One string is prefix of other
  return i == str1.size() && j != str2.size();
}

bool naturalLess(const std::string& str1, const std::string& str2) {
  return naturalLess(std::string_view{str1}, std::string_view{str2});
}

void sortFileList(std::vector<std::string>& strs) {
  std::sort(begin(strs), end(strs), [](const std::string& str1, const std::string& str2) {
    // Directories first
    bool isDir1 = str1.back() == '/';
    bool isDir2 = str2.back() == '/';
    if (isDir1 != isDir2) return isDir1;

    return naturalLess(str1, str2);
  });
}

bool checkFileExtension(std::string_view fileName, const char* extension) {
  const size_t extLen = strlen(extension);
  if (fileName.length() < extLen) {
    return false;
  }

  const size_t offset = fileName.length() - extLen;
  for (size_t i = 0; i < extLen; i++) {
    if (tolower(static_cast<unsigned char>(fileName[offset + i])) !=
        tolower(static_cast<unsigned char>(extension[i]))) {
      return false;
    }
  }
  return true;
}

bool hasJpgExtension(std::string_view fileName) {
  return checkFileExtension(fileName, ".jpg") || checkFileExtension(fileName, ".jpeg");
}

bool hasPngExtension(std::string_view fileName) { return checkFileExtension(fileName, ".png"); }

bool hasBmpExtension(std::string_view fileName) { return checkFileExtension(fileName, ".bmp"); }

bool hasImageExtension(std::string_view fileName) {
  return hasBmpExtension(fileName) || hasPngExtension(fileName) || hasJpgExtension(fileName);
}

bool hasGifExtension(std::string_view fileName) { return checkFileExtension(fileName, ".gif"); }

bool hasEpubExtension(std::string_view fileName) { return checkFileExtension(fileName, ".epub"); }

bool hasXtcExtension(std::string_view fileName) {
  return checkFileExtension(fileName, ".xtc") || checkFileExtension(fileName, ".xtch");
}

bool hasCbzExtension(std::string_view fileName) { return checkFileExtension(fileName, ".cbz"); }

bool hasTxtExtension(std::string_view fileName) { return checkFileExtension(fileName, ".txt"); }

bool hasMarkdownExtension(std::string_view fileName) { return checkFileExtension(fileName, ".md"); }

bool hasReflowableBookExtension(std::string_view fileName) {
  return hasEpubExtension(fileName) || hasTxtExtension(fileName) || hasMarkdownExtension(fileName);
}

bool hasCssExtension(std::string_view fileName) { return checkFileExtension(fileName, ".css"); }

std::string extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

std::string getFileNameWithoutExtension(std::string_view filePath) {
  const auto lastSlash = filePath.find_last_of("/\\");
  std::string_view filename = (lastSlash != std::string_view::npos) ? filePath.substr(lastSlash + 1) : filePath;
  const auto lastDot = filename.find_last_of('.');
  if (lastDot != std::string_view::npos && lastDot > 0) {
    filename = filename.substr(0, lastDot);
  }
  return std::string(filename);
}

bool isSafePathComponent(std::string_view name) {
  return !name.empty() && name.find_first_of("/\\") == std::string_view::npos && name != "." && name != "..";
}

void sanitizePathComponentForFat32(const char* input, char* output, size_t maxLen) {
  if (maxLen == 0) {
    return;
  }

  size_t i = 0;
  for (; i < maxLen - 1 && input[i] != '\0'; i++) {
    const char c = input[i];
    if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
        c == ' ' || (c > 0x00 && c <= 0x1f)) {
      output[i] = '-';
    } else {
      output[i] = c;
    }
  }
  // If the last character was cut in half, drop its leftover bytes.
  i = static_cast<size_t>(utf8SafeTruncateBuffer(output, static_cast<int>(i)));
  output[i] = '\0';
}

}  // namespace FsHelpers
