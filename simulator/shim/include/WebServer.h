#pragma once
// Simulator shim of arduino-esp32 v3's WebServer, driven without sockets:
// the browser frontend (via a service worker) or a native test harness calls
// simulateRequest() and the REAL CrossPointWebServer/WebDAVHandler handler
// code runs against the simulated SD card. Implementation: webserver_shim.cpp.
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "WString.h"
#include "WiFi.h"

// Subset of http_parser's method list used by the firmware (incl. WebDAV).
typedef enum {
  HTTP_ANY = -1,
  HTTP_DELETE = 0,
  HTTP_GET = 1,
  HTTP_HEAD = 2,
  HTTP_POST = 3,
  HTTP_PUT = 4,
  HTTP_OPTIONS = 6,
  HTTP_COPY = 8,
  HTTP_LOCK = 9,
  HTTP_MKCOL = 10,
  HTTP_MOVE = 11,
  HTTP_PROPFIND = 12,
  HTTP_UNLOCK = 15,
  HTTP_PATCH = 28,
} HTTPMethod;

typedef enum { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED } HTTPUploadStatus;
typedef enum { RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED } HTTPRawStatus;

#define HTTP_UPLOAD_BUFLEN 1436
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
#define CONTENT_LENGTH_NOT_SET ((size_t)-2)

class HTTPUpload {
 public:
  HTTPUploadStatus status = UPLOAD_FILE_START;
  String filename;
  String name;
  String type;
  size_t totalSize = 0;    // bytes delivered so far (matches esp32 semantics)
  size_t currentSize = 0;  // bytes in buf for this event
  uint8_t buf[HTTP_UPLOAD_BUFLEN];
};

class HTTPRaw {
 public:
  HTTPRawStatus status = RAW_START;
  size_t totalSize = 0;
  size_t currentSize = 0;
  uint8_t buf[HTTP_UPLOAD_BUFLEN];
  void* data = nullptr;
};

class WebServer;

class RequestHandler {
 public:
  virtual ~RequestHandler() = default;
  virtual bool canHandle(WebServer&, HTTPMethod, const String&) { return false; }
  virtual bool canUpload(WebServer&, const String&) { return false; }
  virtual bool canRaw(WebServer&, const String&) { return false; }
  virtual bool handle(WebServer&, HTTPMethod, const String&) { return false; }
  virtual void upload(WebServer&, const String&, HTTPUpload&) {}
  virtual void raw(WebServer&, const String&, HTTPRaw&) {}
};

class WebServer {
 public:
  typedef std::function<void()> THandlerFunction;

  explicit WebServer(uint16_t port = 80);
  ~WebServer();

  void begin();
  void stop();
  void close() { stop(); }
  void handleClient() {}

  void on(const String& uri, THandlerFunction fn);
  void on(const String& uri, HTTPMethod method, THandlerFunction fn);
  void on(const String& uri, HTTPMethod method, THandlerFunction fn, THandlerFunction ufn);
  void onNotFound(THandlerFunction fn);
  void addHandler(RequestHandler* handler);  // takes ownership, like esp32 v3
  void enableCORS(bool enable = true) { cors_ = enable; }
  void collectHeaders(const char* headerKeys[], size_t count);

  // --- request accessors (valid while a handler runs) ------------------------
  String uri() const { return uri_; }
  HTTPMethod method() const { return method_; }
  String arg(const String& name) const;
  bool hasArg(const String& name) const;
  int args() const { return static_cast<int>(argList_.size()); }
  String argName(int i) const;
  String arg(int i) const;
  String header(const String& name) const;
  bool hasHeader(const String& name) const;
  HTTPUpload& upload() { return upload_; }
  NetworkClient& client() {
    client_.sink = &response_.body;  // raw client writes land in the response
    return client_;
  }
  size_t clientContentLength() const { return bodyLen_; }
  static String urlDecode(const String& text);

  // --- response construction -------------------------------------------------
  void setContentLength(size_t len) { (void)len; }
  void sendHeader(const String& name, const String& value, bool first = false);
  void send(int code, const char* contentType = "", const String& content = String());
  void send(int code, const String& contentType, const String& content);
  void send(int code, const char* contentType, const char* content);
  void send_P(int code, const char* contentType, const char* content, size_t contentLength);
  void sendContent(const String& content);
  void sendContent(const char* content, size_t length);

  // --- simulator entry -------------------------------------------------------
  struct SimResponse {
    int code = 0;
    std::string contentType;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;  // binary-safe
  };
  // methodStr: "GET"/"POST"/"PROPFIND"/...; uriWithQuery: "/api/files?path=/".
  // headerBlock: "Name\nValue\n" pairs. body may be binary (uploads).
  SimResponse simulateRequest(const char* methodStr, const char* uriWithQuery, const uint8_t* body, size_t bodyLen,
                              const char* headerBlock);

  // The server most recently begin()'d and not yet stop()'d (for the bridge).
  static WebServer* activeInstance();

 private:
  struct Route {
    std::string uri;
    HTTPMethod method;
    THandlerFunction fn;
    THandlerFunction uploadFn;
  };

  void resetRequestState();
  void dispatch(const uint8_t* body, size_t bodyLen, const std::string& contentType);
  void runMultipart(const Route* route, RequestHandler* handler, const uint8_t* body, size_t bodyLen,
                    const std::string& boundary);

  uint16_t port_;
  bool running_ = false;
  bool cors_ = false;
  std::vector<Route> routes_;
  std::vector<std::unique_ptr<RequestHandler>> handlers_;
  THandlerFunction notFound_;

  // per-request state
  String uri_;
  HTTPMethod method_ = HTTP_GET;
  std::vector<std::pair<std::string, std::string>> argList_;
  std::vector<std::pair<std::string, std::string>> headersIn_;
  HTTPUpload upload_;
  NetworkClient client_;
  SimResponse response_;
  size_t bodyLen_ = 0;
};
