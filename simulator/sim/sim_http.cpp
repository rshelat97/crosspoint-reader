// Browser bridge into the firmware's web server. The frontend's service
// worker intercepts fetches under its /device/ scope and forwards them here;
// the REAL CrossPointWebServer handlers then run against the simulated SD
// card. Exposed for the native build too (unit smoke tests can call it).
#include <WebServer.h>

#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define SIM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define SIM_EXPORT
#endif

namespace {
WebServer::SimResponse lastResponse;
std::string lastHeaderBlock;  // "Name\nValue\n..." including Content-Type
}  // namespace

extern "C" {

SIM_EXPORT int sim_server_running() { return WebServer::activeInstance() != nullptr ? 1 : 0; }

// Returns the HTTP status code, or 0 when no server is running.
SIM_EXPORT int sim_http_request(const char* method, const char* uriWithQuery, const uint8_t* body, int bodyLen,
                                const char* headerBlock) {
  WebServer* server = WebServer::activeInstance();
  if (!server) {
    lastResponse = WebServer::SimResponse();
    lastHeaderBlock.clear();
    return 0;
  }
  lastResponse = server->simulateRequest(method, uriWithQuery, body, static_cast<size_t>(bodyLen), headerBlock);
  lastHeaderBlock.clear();
  if (!lastResponse.contentType.empty()) {
    lastHeaderBlock += "Content-Type\n" + lastResponse.contentType + "\n";
  }
  for (const auto& kv : lastResponse.headers) {
    lastHeaderBlock += kv.first + "\n" + kv.second + "\n";
  }
  return lastResponse.code;
}

SIM_EXPORT const uint8_t* sim_http_response_body() {
  return reinterpret_cast<const uint8_t*>(lastResponse.body.data());
}
SIM_EXPORT int sim_http_response_body_len() { return static_cast<int>(lastResponse.body.size()); }
SIM_EXPORT const char* sim_http_response_headers() { return lastHeaderBlock.c_str(); }

}  // extern "C"
