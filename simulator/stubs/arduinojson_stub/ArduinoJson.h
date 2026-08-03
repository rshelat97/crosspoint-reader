#pragma once
// Offline-build fallback for ArduinoJson v7: a small, functional JSON DOM
// covering exactly the API surface the firmware's persistence stores use
// (JsonDocument, proxies with operator| defaults, as<T>/is<T>, JsonArray /
// JsonObject iteration, deserializeJson/serializeJson). The CI/web builds
// fetch the real ArduinoJson; this exists so sandboxed native builds work
// without network access. Not a general-purpose reimplementation.
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "WString.h"

namespace minijson {

struct Node {
  enum class Type { Null, Bool, Int, Float, Str, Arr, Obj } type = Type::Null;
  bool b = false;
  long long i = 0;
  double f = 0;
  std::string s;
  std::vector<std::unique_ptr<Node>> arr;
  std::vector<std::pair<std::string, std::unique_ptr<Node>>> obj;

  Node* objGet(const char* key) const {
    for (const auto& kv : obj)
      if (kv.first == key) return kv.second.get();
    return nullptr;
  }
  Node* objGetOrCreate(const char* key) {
    if (Node* n = objGet(key)) return n;
    if (type != Type::Obj) {
      type = Type::Obj;
      arr.clear();
    }
    obj.emplace_back(key, std::make_unique<Node>());
    return obj.back().second.get();
  }
  void setStr(const char* v) {
    type = Type::Str;
    s = v ? v : "";
  }
  double num() const { return type == Type::Int ? static_cast<double>(i) : f; }
};

// --- parser ------------------------------------------------------------------
class Parser {
 public:
  explicit Parser(const char* text) : p(text ? text : "") {}
  bool parse(Node& out) {
    skipWs();
    if (!parseValue(out)) return false;
    skipWs();
    return *p == '\0';
  }

 private:
  const char* p;
  void skipWs() {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  }
  bool parseValue(Node& out) {
    skipWs();
    switch (*p) {
      case '{':
        return parseObj(out);
      case '[':
        return parseArr(out);
      case '"':
        return parseStr(out);
      case 't':
        if (strncmp(p, "true", 4) != 0) return false;
        p += 4;
        out.type = Node::Type::Bool;
        out.b = true;
        return true;
      case 'f':
        if (strncmp(p, "false", 5) != 0) return false;
        p += 5;
        out.type = Node::Type::Bool;
        out.b = false;
        return true;
      case 'n':
        if (strncmp(p, "null", 4) != 0) return false;
        p += 4;
        out.type = Node::Type::Null;
        return true;
      default:
        return parseNum(out);
    }
  }
  bool parseObj(Node& out) {
    p++;  // {
    out.type = Node::Type::Obj;
    skipWs();
    if (*p == '}') {
      p++;
      return true;
    }
    while (true) {
      skipWs();
      Node key;
      if (!parseStr(key)) return false;
      skipWs();
      if (*p != ':') return false;
      p++;
      auto child = std::make_unique<Node>();
      if (!parseValue(*child)) return false;
      out.obj.emplace_back(std::move(key.s), std::move(child));
      skipWs();
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p == '}') {
        p++;
        return true;
      }
      return false;
    }
  }
  bool parseArr(Node& out) {
    p++;  // [
    out.type = Node::Type::Arr;
    skipWs();
    if (*p == ']') {
      p++;
      return true;
    }
    while (true) {
      auto child = std::make_unique<Node>();
      if (!parseValue(*child)) return false;
      out.arr.push_back(std::move(child));
      skipWs();
      if (*p == ',') {
        p++;
        continue;
      }
      if (*p == ']') {
        p++;
        return true;
      }
      return false;
    }
  }
  bool parseStr(Node& out) {
    if (*p != '"') return false;
    p++;
    out.type = Node::Type::Str;
    out.s.clear();
    while (*p && *p != '"') {
      if (*p == '\\') {
        p++;
        switch (*p) {
          case '"':
            out.s += '"';
            break;
          case '\\':
            out.s += '\\';
            break;
          case '/':
            out.s += '/';
            break;
          case 'n':
            out.s += '\n';
            break;
          case 't':
            out.s += '\t';
            break;
          case 'r':
            out.s += '\r';
            break;
          case 'b':
            out.s += '\b';
            break;
          case 'f':
            out.s += '\f';
            break;
          case 'u': {
            // \uXXXX: decode BMP code points to UTF-8 (no surrogate pairs).
            char hex[5] = {p[1], p[2], p[3], p[4], 0};
            const unsigned cp = static_cast<unsigned>(strtoul(hex, nullptr, 16));
            if (cp < 0x80) {
              out.s += static_cast<char>(cp);
            } else if (cp < 0x800) {
              out.s += static_cast<char>(0xC0 | (cp >> 6));
              out.s += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
              out.s += static_cast<char>(0xE0 | (cp >> 12));
              out.s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
              out.s += static_cast<char>(0x80 | (cp & 0x3F));
            }
            p += 4;
            break;
          }
          default:
            return false;
        }
        p++;
      } else {
        out.s += *p++;
      }
    }
    if (*p != '"') return false;
    p++;
    return true;
  }
  bool parseNum(Node& out) {
    char* end = nullptr;
    const double d = strtod(p, &end);
    if (end == p) return false;
    bool isInt = true;
    for (const char* q = p; q < end; q++)
      if (*q == '.' || *q == 'e' || *q == 'E') isInt = false;
    p = end;
    if (isInt) {
      out.type = Node::Type::Int;
      out.i = static_cast<long long>(d);
    } else {
      out.type = Node::Type::Float;
      out.f = d;
    }
    return true;
  }
};

// --- serializer --------------------------------------------------------------
inline void serializeNode(const Node& n, std::string& out) {
  switch (n.type) {
    case Node::Type::Null:
      out += "null";
      break;
    case Node::Type::Bool:
      out += n.b ? "true" : "false";
      break;
    case Node::Type::Int: {
      char buf[24];
      snprintf(buf, sizeof(buf), "%lld", n.i);
      out += buf;
      break;
    }
    case Node::Type::Float: {
      char buf[32];
      snprintf(buf, sizeof(buf), "%.9g", n.f);
      out += buf;
      break;
    }
    case Node::Type::Str:
      out += '"';
      for (char c : n.s) {
        switch (c) {
          case '"':
            out += "\\\"";
            break;
          case '\\':
            out += "\\\\";
            break;
          case '\n':
            out += "\\n";
            break;
          case '\r':
            out += "\\r";
            break;
          case '\t':
            out += "\\t";
            break;
          default:
            if (static_cast<unsigned char>(c) < 0x20) {
              char buf[8];
              snprintf(buf, sizeof(buf), "\\u%04x", c);
              out += buf;
            } else {
              out += c;
            }
        }
      }
      out += '"';
      break;
    case Node::Type::Arr: {
      out += '[';
      bool first = true;
      for (const auto& c : n.arr) {
        if (!first) out += ',';
        first = false;
        serializeNode(*c, out);
      }
      out += ']';
      break;
    }
    case Node::Type::Obj: {
      out += '{';
      bool first = true;
      for (const auto& kv : n.obj) {
        if (!first) out += ',';
        first = false;
        Node key;
        key.setStr(kv.first.c_str());
        serializeNode(key, out);
        out += ':';
        serializeNode(*kv.second, out);
      }
      out += '}';
      break;
    }
  }
}

}  // namespace minijson

// --- ArduinoJson-compatible API ----------------------------------------------

class JsonArray;
class JsonArrayConst;
class JsonObject;
class JsonObjectConst;

// Read-only view over a node (JsonVariantConst).
class JsonVariantConst {
 public:
  JsonVariantConst(const minijson::Node* n = nullptr) : n_(n) {}  // NOLINT

  bool isNull() const { return !n_ || n_->type == minijson::Node::Type::Null; }

  template <typename T>
  T as() const;
  template <typename T>
  bool is() const;

  JsonVariantConst operator[](const char* key) const {
    return JsonVariantConst(n_ && n_->type == minijson::Node::Type::Obj ? n_->objGet(key) : nullptr);
  }

  const minijson::Node* node() const { return n_; }

 protected:
  const minijson::Node* n_;
};

// Default-value operator: value | fallback.
inline const char* operator|(JsonVariantConst v, const char* def) {
  return (!v.isNull() && v.node()->type == minijson::Node::Type::Str) ? v.node()->s.c_str() : def;
}
inline std::string operator|(JsonVariantConst v, const std::string& def) {
  return (!v.isNull() && v.node()->type == minijson::Node::Type::Str) ? v.node()->s : def;
}
inline bool operator|(JsonVariantConst v, bool def) {
  return (!v.isNull() && v.node()->type == minijson::Node::Type::Bool) ? v.node()->b : def;
}
template <typename T>
inline typename std::enable_if<std::is_arithmetic<T>::value, T>::type operator|(JsonVariantConst v, T def) {
  if (v.isNull()) return def;
  const auto t = v.node()->type;
  if (t != minijson::Node::Type::Int && t != minijson::Node::Type::Float) return def;
  return static_cast<T>(v.node()->num());
}

// Mutable proxy over a node.
class JsonVariant : public JsonVariantConst {
 public:
  JsonVariant(minijson::Node* n = nullptr) : JsonVariantConst(n), m_(n) {}  // NOLINT

  JsonVariant operator[](const char* key) { return JsonVariant(m_ ? m_->objGetOrCreate(key) : nullptr); }
  JsonVariantConst operator[](const char* key) const { return JsonVariantConst::operator[](key); }

  JsonVariant& operator=(const char* v) {
    if (m_) m_->setStr(v);
    return *this;
  }
  JsonVariant& operator=(const std::string& v) {
    if (m_) m_->setStr(v.c_str());
    return *this;
  }
  JsonVariant& operator=(const String& v) {
    if (m_) m_->setStr(v.c_str());
    return *this;
  }
  JsonVariant& operator=(bool v) {
    if (m_) {
      m_->type = minijson::Node::Type::Bool;
      m_->b = v;
    }
    return *this;
  }
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
  JsonVariant& operator=(T v) {
    if (m_) {
      m_->type = minijson::Node::Type::Int;
      m_->i = static_cast<long long>(v);
    }
    return *this;
  }
  JsonVariant& operator=(double v) {
    if (m_) {
      m_->type = minijson::Node::Type::Float;
      m_->f = v;
    }
    return *this;
  }
  JsonVariant& operator=(float v) { return operator=(static_cast<double>(v)); }

  template <typename T>
  T to();

  minijson::Node* mnode() const { return m_; }

 protected:
  minijson::Node* m_;
};

// --- arrays ------------------------------------------------------------------
class JsonArrayConst {
 public:
  JsonArrayConst(const minijson::Node* n = nullptr) : n_(n) {}  // NOLINT
  JsonArrayConst(JsonVariantConst v)                            // NOLINT: ArduinoJson allows this
      : n_(v.node() && v.node()->type == minijson::Node::Type::Arr ? v.node() : nullptr) {}
  size_t size() const { return n_ && n_->type == minijson::Node::Type::Arr ? n_->arr.size() : 0; }
  bool isNull() const { return !n_ || n_->type != minijson::Node::Type::Arr; }
  JsonVariantConst operator[](size_t i) const {
    return JsonVariantConst(n_ && i < size() ? n_->arr[i].get() : nullptr);
  }

  class iterator {
   public:
    iterator(const minijson::Node* n, size_t i) : n_(n), i_(i) {}
    bool operator!=(const iterator& o) const { return i_ != o.i_; }
    void operator++() { i_++; }
    JsonVariantConst operator*() const { return JsonVariantConst(n_->arr[i_].get()); }

   private:
    const minijson::Node* n_;
    size_t i_;
  };
  iterator begin() const { return iterator(n_, 0); }
  iterator end() const { return iterator(n_, size()); }

  const minijson::Node* node() const { return n_; }

 private:
  const minijson::Node* n_;
};

class JsonArray {
 public:
  JsonArray(minijson::Node* n = nullptr) : n_(n) {}  // NOLINT
  JsonArray(JsonVariant v)                           // NOLINT: ArduinoJson allows this
      : n_(v.mnode() && v.mnode()->type == minijson::Node::Type::Arr ? v.mnode() : nullptr) {}
  size_t size() const { return n_ && n_->type == minijson::Node::Type::Arr ? n_->arr.size() : 0; }
  bool isNull() const { return !n_ || n_->type != minijson::Node::Type::Arr; }

  template <typename T>
  T add();

  // Value-appending add() overloads (ArduinoJson's add(T value)).
  template <typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
  bool add(T value) {
    if (!n_) return false;
    n_->arr.push_back(std::make_unique<minijson::Node>());
    minijson::Node* c = n_->arr.back().get();
    c->type = minijson::Node::Type::Int;
    c->i = static_cast<long long>(value);
    return true;
  }
  bool add(const char* value) {
    if (!n_) return false;
    n_->arr.push_back(std::make_unique<minijson::Node>());
    n_->arr.back()->setStr(value);
    return true;
  }
  bool add(const std::string& value) { return add(value.c_str()); }

  class iterator {
   public:
    iterator(minijson::Node* n, size_t i) : n_(n), i_(i) {}
    bool operator!=(const iterator& o) const { return i_ != o.i_; }
    void operator++() { i_++; }
    JsonVariant operator*() const { return JsonVariant(n_->arr[i_].get()); }

   private:
    minijson::Node* n_;
    size_t i_;
  };
  iterator begin() const { return iterator(n_, 0); }
  iterator end() const { return iterator(n_, size()); }

  minijson::Node* node() const { return n_; }

  operator JsonArrayConst() const { return JsonArrayConst(n_); }

 private:
  minijson::Node* n_;
};

// --- objects -----------------------------------------------------------------
class JsonObjectConst {
 public:
  JsonObjectConst(const minijson::Node* n = nullptr) : n_(n) {}  // NOLINT
  JsonObjectConst(JsonVariantConst v)                            // NOLINT: ArduinoJson allows this
      : n_(v.node() && v.node()->type == minijson::Node::Type::Obj ? v.node() : nullptr) {}
  bool isNull() const { return !n_ || n_->type != minijson::Node::Type::Obj; }
  JsonVariantConst operator[](const char* key) const {
    return JsonVariantConst(n_ && n_->type == minijson::Node::Type::Obj ? n_->objGet(key) : nullptr);
  }
  const minijson::Node* node() const { return n_; }
  operator JsonVariantConst() const { return JsonVariantConst(n_); }

 private:
  const minijson::Node* n_;
};

class JsonObject {
 public:
  JsonObject(minijson::Node* n = nullptr) : n_(n) {}  // NOLINT
  JsonObject(JsonVariant v)                           // NOLINT: ArduinoJson allows this
      : n_(v.mnode() && v.mnode()->type == minijson::Node::Type::Obj ? v.mnode() : nullptr) {}
  bool isNull() const { return !n_ || n_->type != minijson::Node::Type::Obj; }
  JsonVariant operator[](const char* key) { return JsonVariant(n_ ? n_->objGetOrCreate(key) : nullptr); }
  JsonVariantConst operator[](const char* key) const {
    return JsonVariantConst(n_ && n_->type == minijson::Node::Type::Obj ? n_->objGet(key) : nullptr);
  }
  minijson::Node* node() const { return n_; }
  operator JsonObjectConst() const { return JsonObjectConst(n_); }

 private:
  minijson::Node* n_;
};

// --- as<T> / is<T> / to<T> ---------------------------------------------------
template <>
inline const char* JsonVariantConst::as<const char*>() const {
  return (n_ && n_->type == minijson::Node::Type::Str) ? n_->s.c_str() : "";
}
template <>
inline std::string JsonVariantConst::as<std::string>() const {
  return (n_ && n_->type == minijson::Node::Type::Str) ? n_->s : std::string();
}
template <>
inline String JsonVariantConst::as<String>() const {
  return String(as<std::string>());
}
template <>
inline bool JsonVariantConst::as<bool>() const {
  return n_ && n_->type == minijson::Node::Type::Bool && n_->b;
}
template <>
inline uint8_t JsonVariantConst::as<uint8_t>() const {
  return n_ ? static_cast<uint8_t>(n_->num()) : 0;
}
template <>
inline uint16_t JsonVariantConst::as<uint16_t>() const {
  return n_ ? static_cast<uint16_t>(n_->num()) : 0;
}
template <>
inline uint32_t JsonVariantConst::as<uint32_t>() const {
  return n_ ? static_cast<uint32_t>(n_->num()) : 0;
}
template <>
inline int JsonVariantConst::as<int>() const {
  return n_ ? static_cast<int>(n_->num()) : 0;
}
template <>
inline long JsonVariantConst::as<long>() const {
  return n_ ? static_cast<long>(n_->num()) : 0;
}
template <>
inline float JsonVariantConst::as<float>() const {
  return n_ ? static_cast<float>(n_->num()) : 0;
}
template <>
inline double JsonVariantConst::as<double>() const {
  return n_ ? n_->num() : 0;
}
template <>
inline JsonVariantConst JsonVariantConst::as<JsonVariantConst>() const {
  return *this;
}
template <>
inline JsonArrayConst JsonVariantConst::as<JsonArrayConst>() const {
  return JsonArrayConst(n_);
}
// Mutable-view escapes for calls made through JsonVariant (which inherits the
// base template): safe in this stub because JsonVariant only wraps mutable docs.
template <>
inline JsonArray JsonVariantConst::as<JsonArray>() const {
  return JsonArray(const_cast<minijson::Node*>(n_ && n_->type == minijson::Node::Type::Arr ? n_ : nullptr));
}
template <>
inline JsonObject JsonVariantConst::as<JsonObject>() const {
  return JsonObject(const_cast<minijson::Node*>(n_ && n_->type == minijson::Node::Type::Obj ? n_ : nullptr));
}
template <>
inline JsonObjectConst JsonVariantConst::as<JsonObjectConst>() const {
  return JsonObjectConst(n_);
}

template <>
inline bool JsonVariantConst::is<const char*>() const {
  return n_ && n_->type == minijson::Node::Type::Str;
}
template <>
inline bool JsonVariantConst::is<std::string>() const {
  return is<const char*>();
}
template <>
inline bool JsonVariantConst::is<bool>() const {
  return n_ && n_->type == minijson::Node::Type::Bool;
}
template <>
inline bool JsonVariantConst::is<uint8_t>() const {
  return n_ && (n_->type == minijson::Node::Type::Int || n_->type == minijson::Node::Type::Float);
}
template <>
inline bool JsonVariantConst::is<uint16_t>() const {
  return is<uint8_t>();
}
template <>
inline bool JsonVariantConst::is<uint32_t>() const {
  return is<uint8_t>();
}
template <>
inline bool JsonVariantConst::is<int>() const {
  return is<uint8_t>();
}
template <>
inline bool JsonVariantConst::is<float>() const {
  return is<uint8_t>();
}
template <>
inline bool JsonVariantConst::is<JsonArrayConst>() const {
  return n_ && n_->type == minijson::Node::Type::Arr;
}
template <>
inline bool JsonVariantConst::is<JsonObjectConst>() const {
  return n_ && n_->type == minijson::Node::Type::Obj;
}

// JsonVariant read-through: reuse the const view's specializations.
// (as<JsonArray>/as<JsonObject> on the mutable side.)
template <>
inline JsonVariant JsonVariant::to<JsonVariant>() {
  return *this;
}
template <>
inline JsonArray JsonVariant::to<JsonArray>() {
  if (m_) {
    m_->type = minijson::Node::Type::Arr;
    m_->arr.clear();
    m_->obj.clear();
  }
  return JsonArray(m_);
}
template <>
inline JsonObject JsonVariant::to<JsonObject>() {
  if (m_) {
    m_->type = minijson::Node::Type::Obj;
    m_->arr.clear();
    m_->obj.clear();
  }
  return JsonObject(m_);
}

template <>
inline JsonObject JsonArray::add<JsonObject>() {
  if (!n_) return JsonObject(nullptr);
  if (n_->type != minijson::Node::Type::Arr) {
    n_->type = minijson::Node::Type::Arr;
    n_->obj.clear();
  }
  n_->arr.push_back(std::make_unique<minijson::Node>());
  n_->arr.back()->type = minijson::Node::Type::Obj;
  return JsonObject(n_->arr.back().get());
}
template <>
inline JsonVariant JsonArray::add<JsonVariant>() {
  if (!n_) return JsonVariant(nullptr);
  n_->arr.push_back(std::make_unique<minijson::Node>());
  return JsonVariant(n_->arr.back().get());
}

// --- document ----------------------------------------------------------------
class JsonDocument {
 public:
  JsonDocument() : root_(std::make_unique<minijson::Node>()) {}

  JsonVariant operator[](const char* key) {
    if (root_->type != minijson::Node::Type::Obj) root_->type = minijson::Node::Type::Obj;
    return JsonVariant(root_->objGetOrCreate(key));
  }
  JsonVariantConst operator[](const char* key) const {
    return JsonVariantConst(root_->type == minijson::Node::Type::Obj ? root_->objGet(key) : nullptr);
  }

  template <typename T>
  T as() const {
    return JsonVariantConst(root_.get()).as<T>();
  }
  template <typename T>
  T to() {
    return JsonVariant(root_.get()).to<T>();
  }

  void clear() { *root_ = minijson::Node(); }
  bool overflowed() const { return false; }

  operator JsonVariantConst() const { return JsonVariantConst(root_.get()); }
  operator JsonVariant() { return JsonVariant(root_.get()); }

  minijson::Node* root() { return root_.get(); }
  const minijson::Node* root() const { return root_.get(); }

 private:
  std::unique_ptr<minijson::Node> root_;
};

// --- (de)serialization -------------------------------------------------------
class DeserializationError {
 public:
  enum Code { Ok = 0, EmptyInput, IncompleteInput, InvalidInput, NoMemory, TooDeep };
  DeserializationError(Code c = Ok) : c_(c) {}  // NOLINT
  explicit operator bool() const { return c_ != Ok; }
  bool operator==(Code c) const { return c_ == c; }
  const char* c_str() const {
    switch (c_) {
      case Ok:
        return "Ok";
      case EmptyInput:
        return "EmptyInput";
      default:
        return "InvalidInput";
    }
  }

 private:
  Code c_;
};

inline DeserializationError deserializeJson(JsonDocument& doc, const char* input) {
  doc.clear();
  if (!input || !*input) return DeserializationError::EmptyInput;
  minijson::Parser parser(input);
  return parser.parse(*doc.root()) ? DeserializationError::Ok : DeserializationError::InvalidInput;
}
inline DeserializationError deserializeJson(JsonDocument& doc, const std::string& input) {
  return deserializeJson(doc, input.c_str());
}
inline DeserializationError deserializeJson(JsonDocument& doc, const String& input) {
  return deserializeJson(doc, input.c_str());
}

// Stream-style input (e.g. HalFile): anything with read(void*, size_t) + size().
template <typename TStream>
inline auto deserializeJson(JsonDocument& doc, TStream& stream)
    -> decltype(stream.read(static_cast<void*>(nullptr), size_t(0)), stream.size(), DeserializationError()) {
  std::string content;
  content.resize(stream.size());
  const int n = stream.read(content.data(), content.size());
  content.resize(n > 0 ? static_cast<size_t>(n) : 0);
  return deserializeJson(doc, content);
}

inline size_t serializeJson(const JsonDocument& doc, std::string& out) {
  out.clear();
  minijson::serializeNode(*doc.root(), out);
  return out.size();
}
inline size_t serializeJson(const JsonDocument& doc, String& out) {
  std::string tmp;
  minijson::serializeNode(*doc.root(), tmp);
  out = String(tmp);
  return tmp.size();
}
