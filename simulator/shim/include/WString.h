#pragma once
// Simulator shim for Arduino's String, backed by std::string. Only the
// operations the firmware actually uses (see simulator research notes) are
// provided; String must be a real class (SettingsActivity has a static member
// function named String, so a macro would break the build).
#include <cctype>
#include <cstdint>  // Arduino's WString.h chain provides fixed-width ints
#include <cstring>
#include <string>

class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}  // NOLINT(google-explicit-constructor)
  // Explicit: Arduino's String has no std::string constructor, and an implicit
  // one makes firmware overload sets (std::string_view vs String) ambiguous.
  explicit String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}                        // NOLINT(google-explicit-constructor)
  String(int v) : s_(std::to_string(v)) {}            // NOLINT(google-explicit-constructor)
  String(unsigned int v) : s_(std::to_string(v)) {}   // NOLINT(google-explicit-constructor)
  String(long v) : s_(std::to_string(v)) {}           // NOLINT(google-explicit-constructor)
  String(unsigned long v) : s_(std::to_string(v)) {}  // NOLINT(google-explicit-constructor)

  const char* c_str() const { return s_.c_str(); }
  unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
  bool isEmpty() const { return s_.empty(); }

  bool startsWith(const char* prefix) const { return s_.rfind(prefix, 0) == 0; }
  bool startsWith(const String& prefix) const { return s_.rfind(prefix.s_, 0) == 0; }
  bool endsWith(const char* suffix) const {
    const size_t n = strlen(suffix);
    return s_.size() >= n && s_.compare(s_.size() - n, n, suffix) == 0;
  }
  bool endsWith(const String& suffix) const { return endsWith(suffix.c_str()); }

  String substring(unsigned int from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
  String substring(unsigned int from, unsigned int to) const {
    if (from >= s_.size() || to <= from) return String();
    return String(s_.substr(from, to - from));
  }

  int indexOf(char c) const {
    const auto p = s_.find(c);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int indexOf(const char* sub) const {
    const auto p = s_.find(sub);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int lastIndexOf(char c) const {
    const auto p = s_.rfind(c);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }

  void trim() {
    const char* ws = " \t\r\n";
    const auto b = s_.find_first_not_of(ws);
    if (b == std::string::npos) {
      s_.clear();
      return;
    }
    const auto e = s_.find_last_not_of(ws);
    s_ = s_.substr(b, e - b + 1);
  }

  void toLowerCase() {
    for (auto& c : s_) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  void toUpperCase() {
    for (auto& c : s_) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }

  char operator[](unsigned int i) const { return i < s_.size() ? s_[i] : '\0'; }
  char charAt(unsigned int i) const { return (*this)[i]; }

  String& operator+=(const char* rhs) {
    s_ += rhs;
    return *this;
  }
  String& operator+=(const String& rhs) {
    s_ += rhs.s_;
    return *this;
  }
  String& operator+=(char c) {
    s_ += c;
    return *this;
  }

  friend String operator+(String lhs, const String& rhs) { return String(lhs.s_ + rhs.s_); }
  friend String operator+(String lhs, const char* rhs) { return String(lhs.s_ + rhs); }
  friend String operator+(const char* lhs, const String& rhs) { return String(lhs + rhs.s_); }

  bool operator==(const char* rhs) const { return s_ == rhs; }
  bool operator==(const String& rhs) const { return s_ == rhs.s_; }
  bool operator!=(const char* rhs) const { return s_ != rhs; }
  bool operator!=(const String& rhs) const { return s_ != rhs.s_; }
  bool operator<(const String& rhs) const { return s_ < rhs.s_; }

  const std::string& str() const { return s_; }

 private:
  std::string s_;
};
