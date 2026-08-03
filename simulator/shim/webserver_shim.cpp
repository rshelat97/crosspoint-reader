// WebServer shim implementation: HTTP semantics without sockets. Requests are
// injected by the browser bridge (sim/sim_http.cpp) or native tests; the REAL
// firmware handlers run unmodified against the simulated SD card. Mirrors the
// arduino-esp32 v3 behaviours the firmware depends on: query/form args, the
// "plain" arg for raw bodies, multipart upload events, and raw-body streaming
// for WebDAV PUT.
#include <WebServer.h>

#include <algorithm>
#include <cctype>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {

WebServer* g_active = nullptr;

std::string urlDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] == '+') {
      out += ' ';
    } else if (in[i] == '%' && i + 2 < in.size() && isxdigit(static_cast<unsigned char>(in[i + 1])) &&
               isxdigit(static_cast<unsigned char>(in[i + 2]))) {
      const char hex[3] = {in[i + 1], in[i + 2], 0};
      out += static_cast<char>(strtol(hex, nullptr, 16));
      i += 2;
    } else {
      out += in[i];
    }
  }
  return out;
}

void parsePairs(const std::string& s, std::vector<std::pair<std::string, std::string>>& out) {
  size_t pos = 0;
  while (pos < s.size()) {
    size_t amp = s.find('&', pos);
    if (amp == std::string::npos) amp = s.size();
    const std::string pair = s.substr(pos, amp - pos);
    const size_t eq = pair.find('=');
    if (eq == std::string::npos) {
      if (!pair.empty()) out.emplace_back(urlDecode(pair), "");
    } else {
      out.emplace_back(urlDecode(pair.substr(0, eq)), urlDecode(pair.substr(eq + 1)));
    }
    pos = amp + 1;
  }
}

bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

HTTPMethod methodFromString(const char* m) {
  const std::string s = m ? m : "GET";
  if (s == "GET") return HTTP_GET;
  if (s == "POST") return HTTP_POST;
  if (s == "PUT") return HTTP_PUT;
  if (s == "DELETE") return HTTP_DELETE;
  if (s == "HEAD") return HTTP_HEAD;
  if (s == "OPTIONS") return HTTP_OPTIONS;
  if (s == "PATCH") return HTTP_PATCH;
  if (s == "PROPFIND") return HTTP_PROPFIND;
  if (s == "MKCOL") return HTTP_MKCOL;
  if (s == "MOVE") return HTTP_MOVE;
  if (s == "COPY") return HTTP_COPY;
  if (s == "LOCK") return HTTP_LOCK;
  if (s == "UNLOCK") return HTTP_UNLOCK;
  return HTTP_GET;
}

// Extract a quoted parameter (name="x") from a Content-Disposition line.
std::string dispositionParam(const std::string& disposition, const char* key) {
  const std::string needle = std::string(key) + "=\"";
  const size_t start = disposition.find(needle);
  if (start == std::string::npos) return "";
  const size_t valueStart = start + needle.size();
  const size_t end = disposition.find('"', valueStart);
  if (end == std::string::npos) return "";
  return disposition.substr(valueStart, end - valueStart);
}

}  // namespace

String WebServer::urlDecode(const String& text) { return String(::urlDecode(std::string(text.c_str()))); }

WebServer::WebServer(uint16_t port) : port_(port) {}
WebServer::~WebServer() {
  if (g_active == this) g_active = nullptr;
}

WebServer* WebServer::activeInstance() { return g_active; }

void WebServer::begin() {
  running_ = true;
  g_active = this;
#ifdef __EMSCRIPTEN__
  EM_ASM({
    if (Module.onDeviceServer) Module.onDeviceServer(1);
  });
#endif
}

void WebServer::stop() {
  running_ = false;
  if (g_active == this) g_active = nullptr;
#ifdef __EMSCRIPTEN__
  EM_ASM({
    if (Module.onDeviceServer) Module.onDeviceServer(0);
  });
#endif
}

void WebServer::on(const String& uri, THandlerFunction fn) { routes_.push_back({uri.c_str(), HTTP_ANY, fn, nullptr}); }
void WebServer::on(const String& uri, HTTPMethod method, THandlerFunction fn) {
  routes_.push_back({uri.c_str(), method, fn, nullptr});
}
void WebServer::on(const String& uri, HTTPMethod method, THandlerFunction fn, THandlerFunction ufn) {
  routes_.push_back({uri.c_str(), method, fn, ufn});
}
void WebServer::onNotFound(THandlerFunction fn) { notFound_ = fn; }
void WebServer::addHandler(RequestHandler* handler) { handlers_.emplace_back(handler); }
void WebServer::collectHeaders(const char**, size_t) {}  // all headers are stored anyway

String WebServer::arg(const String& name) const {
  for (const auto& kv : argList_) {
    if (kv.first == name.c_str()) return String(kv.second);
  }
  return String();
}
bool WebServer::hasArg(const String& name) const {
  for (const auto& kv : argList_) {
    if (kv.first == name.c_str()) return true;
  }
  return false;
}
String WebServer::argName(int i) const {
  return i >= 0 && i < static_cast<int>(argList_.size()) ? String(argList_[i].first) : String();
}
String WebServer::arg(int i) const {
  return i >= 0 && i < static_cast<int>(argList_.size()) ? String(argList_[i].second) : String();
}
String WebServer::header(const String& name) const {
  for (const auto& kv : headersIn_) {
    if (iequals(kv.first, name.c_str())) return String(kv.second);
  }
  return String();
}
bool WebServer::hasHeader(const String& name) const {
  for (const auto& kv : headersIn_) {
    if (iequals(kv.first, name.c_str())) return true;
  }
  return false;
}

void WebServer::sendHeader(const String& name, const String& value, bool) {
  response_.headers.emplace_back(name.c_str(), value.c_str());
}
void WebServer::send(int code, const char* contentType, const String& content) {
  response_.code = code;
  response_.contentType = contentType ? contentType : "";
  response_.body.assign(content.c_str(), content.length());
}
void WebServer::send(int code, const String& contentType, const String& content) {
  send(code, contentType.c_str(), content);
}
void WebServer::send(int code, const char* contentType, const char* content) {
  send(code, contentType, String(content ? content : ""));
}
void WebServer::send_P(int code, const char* contentType, const char* content, size_t contentLength) {
  response_.code = code;
  response_.contentType = contentType ? contentType : "";
  response_.body.assign(content, contentLength);
}
void WebServer::sendContent(const String& content) { response_.body.append(content.c_str(), content.length()); }
void WebServer::sendContent(const char* content, size_t length) { response_.body.append(content, length); }

void WebServer::resetRequestState() {
  argList_.clear();
  headersIn_.clear();
  upload_ = HTTPUpload();
  response_ = SimResponse();
}

void WebServer::runMultipart(const Route* route, RequestHandler* handler, const uint8_t* body, size_t bodyLen,
                             const std::string& boundary) {
  const std::string data(reinterpret_cast<const char*>(body), bodyLen);
  const std::string delim = "--" + boundary;
  size_t pos = data.find(delim);
  while (pos != std::string::npos) {
    pos += delim.size();
    if (data.compare(pos, 2, "--") == 0) break;  // final boundary
    if (data.compare(pos, 2, "\r\n") == 0) pos += 2;
    const size_t headerEnd = data.find("\r\n\r\n", pos);
    if (headerEnd == std::string::npos) break;
    const std::string partHeaders = data.substr(pos, headerEnd - pos);
    size_t partStart = headerEnd + 4;
    size_t partEnd = data.find("\r\n" + delim, partStart);
    if (partEnd == std::string::npos) partEnd = data.size();

    std::string name, filename, type = "application/octet-stream";
    size_t lineStart = 0;
    while (lineStart < partHeaders.size()) {
      size_t lineEnd = partHeaders.find("\r\n", lineStart);
      if (lineEnd == std::string::npos) lineEnd = partHeaders.size();
      const std::string line = partHeaders.substr(lineStart, lineEnd - lineStart);
      if (line.rfind("Content-Disposition:", 0) == 0 || line.rfind("content-disposition:", 0) == 0) {
        name = dispositionParam(line, "name");
        filename = dispositionParam(line, "filename");
      } else if (line.rfind("Content-Type:", 0) == 0 || line.rfind("content-type:", 0) == 0) {
        type = line.substr(line.find(':') + 1);
        while (!type.empty() && type.front() == ' ') type.erase(type.begin());
      }
      lineStart = lineEnd + 2;
    }

    if (filename.empty()) {
      argList_.emplace_back(name, data.substr(partStart, partEnd - partStart));
    } else {
      auto deliver = [&](HTTPUploadStatus status, const uint8_t* chunk, size_t len) {
        upload_.status = status;
        upload_.currentSize = len;
        if (chunk && len) memcpy(upload_.buf, chunk, len);
        if (route && route->uploadFn) {
          route->uploadFn();
        } else if (handler) {
          handler->upload(*this, uri_, upload_);
        }
        upload_.totalSize += len;
      };
      upload_ = HTTPUpload();
      upload_.filename = String(filename);
      upload_.name = String(name);
      upload_.type = String(type);
      deliver(UPLOAD_FILE_START, nullptr, 0);
      size_t offset = partStart;
      while (offset < partEnd) {
        const size_t len = std::min<size_t>(HTTP_UPLOAD_BUFLEN, partEnd - offset);
        deliver(UPLOAD_FILE_WRITE, reinterpret_cast<const uint8_t*>(data.data()) + offset, len);
        offset += len;
      }
      deliver(UPLOAD_FILE_END, nullptr, 0);
    }
    pos = data.find(delim, partEnd);
  }
}

void WebServer::dispatch(const uint8_t* body, size_t bodyLen, const std::string& contentType) {
  // Routes first (register order), then RequestHandlers (WebDAV) — matching
  // the esp32 server's handler-list semantics.
  const std::string uriStr = uri_.c_str();
  Route* route = nullptr;
  for (auto& r : routes_) {
    if (r.uri == uriStr && (r.method == HTTP_ANY || r.method == method_)) {
      route = &r;
      break;
    }
  }
  RequestHandler* handler = nullptr;
  if (!route) {
    for (auto& h : handlers_) {
      if (h->canHandle(*this, method_, uri_)) {
        handler = h.get();
        break;
      }
    }
  }

  const bool isMultipart = contentType.rfind("multipart/form-data", 0) == 0;
  const bool isForm = contentType.rfind("application/x-www-form-urlencoded", 0) == 0;

  if (isMultipart && (route || handler)) {
    const size_t bpos = contentType.find("boundary=");
    if (bpos != std::string::npos) {
      std::string boundary = contentType.substr(bpos + 9);
      if (boundary.size() >= 2 && boundary.front() == '"' && boundary.back() == '"') {
        boundary = boundary.substr(1, boundary.size() - 2);
      }
      runMultipart(route, handler, body, bodyLen, boundary);
    }
  } else if (isForm && bodyLen) {
    parsePairs(std::string(reinterpret_cast<const char*>(body), bodyLen), argList_);
  } else if (bodyLen) {
    if (handler && handler->canRaw(*this, uri_)) {
      // WebDAV PUT: deliver the body as raw chunks before handle().
      HTTPRaw raw;
      raw.status = RAW_START;
      handler->raw(*this, uri_, raw);
      size_t offset = 0;
      while (offset < bodyLen) {
        const size_t len = std::min<size_t>(HTTP_UPLOAD_BUFLEN, bodyLen - offset);
        raw.status = RAW_WRITE;
        raw.currentSize = len;
        memcpy(raw.buf, body + offset, len);
        handler->raw(*this, uri_, raw);
        raw.totalSize += len;
        offset += len;
      }
      raw.status = RAW_END;
      raw.currentSize = 0;
      handler->raw(*this, uri_, raw);
    } else {
      argList_.emplace_back("plain", std::string(reinterpret_cast<const char*>(body), bodyLen));
    }
  }

  if (route) {
    route->fn();
  } else if (handler) {
    handler->handle(*this, method_, uri_);
  } else if (notFound_) {
    notFound_();
  } else {
    send(404, "text/plain", "Not found");
  }
}

WebServer::SimResponse WebServer::simulateRequest(const char* methodStr, const char* uriWithQuery, const uint8_t* body,
                                                  size_t bodyLen, const char* headerBlock) {
  resetRequestState();
  method_ = methodFromString(methodStr);
  bodyLen_ = bodyLen;

  std::string full = uriWithQuery ? uriWithQuery : "/";
  std::string contentType;
  const size_t q = full.find('?');
  if (q != std::string::npos) {
    parsePairs(full.substr(q + 1), argList_);
    full = full.substr(0, q);
  }
  uri_ = String(::urlDecode(full));  // the file-local helper, not the member

  if (headerBlock) {
    std::string hb = headerBlock;
    size_t pos = 0;
    while (pos < hb.size()) {
      const size_t nameEnd = hb.find('\n', pos);
      if (nameEnd == std::string::npos) break;
      const size_t valueEnd = hb.find('\n', nameEnd + 1);
      if (valueEnd == std::string::npos) break;
      const std::string name = hb.substr(pos, nameEnd - pos);
      const std::string value = hb.substr(nameEnd + 1, valueEnd - nameEnd - 1);
      headersIn_.emplace_back(name, value);
      if (iequals(name, "Content-Type")) contentType = value;
      pos = valueEnd + 1;
    }
  }

  if (!running_) {
    send(503, "text/plain", "Server not running");
    return response_;
  }

  dispatch(body, bodyLen, contentType);

  if (response_.code == 0) send(404, "text/plain", "Not found");
  if (cors_) response_.headers.emplace_back("Access-Control-Allow-Origin", "*");
  return response_;
}
