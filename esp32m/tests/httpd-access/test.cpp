#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "esp32m/ui/httpd-access.hpp"

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = -2,
              ESP_ERR_NOT_FOUND = -3, ESP_ERR_INVALID_STATE = -4;
constexpr int HTTP_GET = 1, HTTP_POST = 2;
#define HTTPD_401 "401 UNAUTHORIZED"
#define CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT 1
#define CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT 1
#define ESP_CHECK_RETURN(expression) do { const auto error = (expression); if (error != ESP_OK) return error; } while (0)
#define logw(...) ((void)0)

struct httpd_req_t {
  void* handle = nullptr;
  void* user_ctx = nullptr;
  const char* uri = "/logs";
  int method = HTTP_GET;
  int fd = 7;
  std::string authorization;
  bool headerError = false;
  std::string status;
  std::map<std::string, std::string> headers;
  int responses = 0;
};

struct httpd_uri_t {
  esp_err_t (*handler)(httpd_req_t*) = nullptr;
  esp_err_t (*ws_pre_handshake_cb)(httpd_req_t*) = nullptr;
  esp_err_t (*ws_post_handshake_cb)(httpd_req_t*) = nullptr;
};

size_t httpd_req_get_hdr_value_len(httpd_req_t* req, const char*) {
  return req->authorization.size();
}
int httpd_req_get_hdr_value_str(httpd_req_t* req, const char*, char* buffer,
                                size_t size) {
  if (req->headerError || size <= req->authorization.size())
    return ESP_FAIL;
  memcpy(buffer, req->authorization.data(), req->authorization.size());
  buffer[req->authorization.size()] = 0;
  return ESP_OK;
}
int httpd_resp_set_status(httpd_req_t* req, const char* status) {
  req->status = status;
  return ESP_OK;
}
int httpd_resp_set_hdr(httpd_req_t* req, const char* name, const char* value) {
  req->headers[name] = value;
  return ESP_OK;
}
int httpd_resp_send(httpd_req_t* req, const char*, size_t) {
  ++req->responses;
  return ESP_OK;
}
int httpd_req_to_sockfd(httpd_req_t* req) { return req->fd; }

// The platform encoder is replaced; credential generation and its checks are
// still the production http_auth_basic implementation.
int esp_crypto_base64_encode(unsigned char* out, size_t capacity, size_t* written,
                             const unsigned char* input, size_t length) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  *written = ((length + 2) / 3) * 4;
  if (!out || capacity < *written + 1) {
    ++*written;
    return ESP_FAIL;
  }
  size_t offset = 0;
  for (size_t i = 0; i < length; i += 3) {
    const unsigned a = input[i], b = i + 1 < length ? input[i + 1] : 0,
                   c = i + 2 < length ? input[i + 2] : 0;
    out[offset++] = alphabet[a >> 2];
    out[offset++] = alphabet[((a & 3) << 4) | (b >> 4)];
    out[offset++] = i + 1 < length ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
    out[offset++] = i + 2 < length ? alphabet[c & 63] : '=';
  }
  out[offset] = 0;
  return ESP_OK;
}

namespace esp32m::ui {
  struct Auth {
    bool enabled = true;
    std::string username = "user", password = "pass";
  };
  struct Ui {
    Auth value;
    const Auth& auth() const { return value; }
  };
  struct IHttpHandler {
    const char* path;
    int verb;
    int calls = 0;
    const char* uri() const { return path; }
    int method() const { return verb; }
    int handle(httpd_req_t*) { ++calls; return ESP_OK; }
  };
  struct IWSHandler {
    const char* path;
    int calls = 0;
    int closes = 0;
    bool accepts = true;
    const char* uri() const { return path; }
    int handle(httpd_req_t*) { ++calls; return accepts ? ESP_OK : ESP_FAIL; }
    void sessionClosed(int) { ++closes; }
  };
  class Httpd {
   public:
    void* _server = this;
    Ui* _ui;
    std::list<IHttpHandler*> _httpHandlers;
    std::list<IWSHandler*> _wsHandlers;
    std::mutex _wsSessionMutex;
    std::map<int, IWSHandler*> _wsSessions;
    std::map<int, uint64_t> _controlSessions;
    esp_err_t authenticate(httpd_req_t*, bool&);
    static esp_err_t httpHandlerCustom(httpd_req_t*);
    static esp_err_t wsHandlerCustom(httpd_req_t*);
    static esp_err_t wsPreHandshake(httpd_req_t*);
    static esp_err_t wsPostHandshake(httpd_req_t*) { return ESP_OK; }
    static void prepareWsHandler(httpd_uri_t&);
    void wsSessionClosed(int);
  };
  std::vector<Httpd*> _httpdServers;
}

#include "production.inc"

int main() {
  using namespace esp32m::ui;
  const char* valid = "Basic dXNlcjpwYXNz";
  Ui ui;
  Httpd server;
  server._ui = &ui;
  _httpdServers.push_back(&server);
  IHttpHandler get{"/logs", HTTP_GET}, post{"/logs", HTTP_POST};
  server._httpHandlers = {nullptr, &post, &get};
  IWSHandler uart{"/ws/uart"};
  server._wsHandlers = {nullptr, &uart};

  for (const std::string& authorization : std::vector<std::string>{
           "", "Basic", "Basic ", "Basic dXNlcjpwYXN", "Basic dXNlcjpwYXNzextra",
           "Basic eDp5", "Bearer dXNlcjpwYXNz", "basic dXNlcjpwYXNz",
           " Basic dXNlcjpwYXNz", "Basic dXNlcjpwYXNz ",
           std::string(valid) + std::string(1, '\0') + "hidden"}) {
    httpd_req_t req;
    req.handle = server._server;
    req.authorization = authorization;
    bool granted = true;
    assert(server.authenticate(&req, granted) == ESP_OK && !granted);
    assert(req.responses == 1 && req.status == HTTPD_401);
    assert(req.headers.at("WWW-Authenticate") == "Basic realm=\"ESP32M\"");
    assert(Httpd::httpHandlerCustom(&req) == ESP_OK);
    assert(get.calls == 0 && post.calls == 0);
    req.uri = "/ws/uart?q=1";
    assert(Httpd::wsHandlerCustom(&req) == ESP_OK);
    assert(uart.calls == 0 && server._wsSessions.empty());
  }
  {
    httpd_req_t req;
    req.authorization = valid;
    req.headerError = true;
    bool granted = true;
    assert(server.authenticate(&req, granted) == ESP_OK && !granted);
    assert(req.responses == 1);
  }
  for (const auto method : {HTTP_GET, HTTP_POST}) {
    httpd_req_t req;
    req.handle = server._server;
    req.uri = "/logs?id=7&download=1";
    req.method = method;
    req.authorization = valid;
    assert(Httpd::httpHandlerCustom(&req) == ESP_OK);
    assert(req.responses == 0 && req.status.empty());
  }
  assert(get.calls == 1 && post.calls == 1);
  {
    httpd_req_t req;
    req.handle = server._server;
    req.uri = "/ws/uart?q=1";
    req.authorization = valid;
    assert(Httpd::wsHandlerCustom(&req) == ESP_OK && uart.calls == 1);
    assert(server._wsSessions.at(req.fd) == &uart);
    req.method = HTTP_POST;  // Host stand-in for a WebSocket data frame.
    req.uri = "";
    assert(Httpd::wsHandlerCustom(&req) == ESP_OK && uart.calls == 2);
    server._wsSessions.clear();
    req.uri = "/ws/uart?fallback=1";
    assert(Httpd::wsHandlerCustom(&req) == ESP_OK && uart.calls == 3);
    req.method = HTTP_GET;
    req.uri = "/ws/uart-extra";
    assert(Httpd::wsHandlerCustom(&req) == ESP_ERR_NOT_FOUND);
    assert(uart.calls == 3);
  }
  for (const auto* uri : {"/logs/extra", "/logs-other", "/logs%3fdownload=1"}) {
    httpd_req_t req;
    req.handle = server._server;
    req.uri = uri;
    req.authorization = valid;
    assert(Httpd::httpHandlerCustom(&req) == ESP_ERR_NOT_FOUND);
  }
  assert(get.calls == 1 && post.calls == 1);
  {
    httpd_req_t req;
    req.handle = server._server;
    req.authorization = valid;
    req.method = 99;
    assert(Httpd::httpHandlerCustom(&req) == ESP_ERR_NOT_FOUND);
  }
  ui.value.enabled = false;
  {
    httpd_req_t req;
    bool granted = false;
    assert(server.authenticate(&req, granted) == ESP_OK && granted);
    assert(req.responses == 0);
  }

  for (const auto* path : {"/logs", "/logs?id=1", "/logs?"})
    assert(uriMatcher("/logs", path, strlen(path)));
  for (const auto* path : {"/logsx", "/logs/", "/logs%3f", "/log"})
    assert(!uriMatcher("/logs", path, strlen(path)));
  for (const auto* path : {"/ws", "/ws?token=x", "/ws/uart", "/ws/uart?q=1"})
    assert(!uriMatcher("/", path, strlen(path)));
  for (const auto* path : {"/", "/settings", "/ws-other", "/wss"})
    assert(uriMatcher("/", path, strlen(path)));
  assert(uriMatcher("/ws", "/ws?token=x", 3));
  assert(!uriMatcher("/ws", "/ws/uart", 8));
  assert(uriMatcher("/ws/uart", "/ws/uart?q=1", 8));
  assert(!uriMatcher("/ws/uart", "/ws/uart2", 9));
  assert(!uriMatcher(nullptr, "/logs", 5));
  assert(!uriMatcher("/logs", nullptr, 0));
  assert(!http::basicAuthorizationMatches(valid, nullptr, 0));
  assert(!http::basicAuthorizationMatches(nullptr, valid, strlen(valid)));
  assert(http::basicAuthorizationMatches(valid, valid, strlen(valid)));
  // ESP-IDF6.1 runs PRE, responds101, then POST, and skips the ordinary URI
  // handler for the GET upgrade. Authentication/custom rejection must happen
  // in PRE, while the control generation is established exclusively in POST.
  ui.value.enabled = true;
  httpd_uri_t control;
  control.handler = [](httpd_req_t*) { return ESP_OK; };
  Httpd::prepareWsHandler(control);
  assert(control.ws_pre_handshake_cb && control.ws_post_handshake_cb);
  {
    httpd_req_t request; request.handle = server._server;
    request.user_ctx = &server; request.uri = "/ws";
    assert(control.ws_pre_handshake_cb(&request) == ESP_FAIL);
    assert(request.status == HTTPD_401 && request.responses == 1);
    request.authorization = valid;
    assert(control.ws_pre_handshake_cb(&request) == ESP_OK);
  }
  httpd_uri_t custom;
  custom.handler = Httpd::wsHandlerCustom;
  Httpd::prepareWsHandler(custom);
  assert(custom.ws_pre_handshake_cb && !custom.ws_post_handshake_cb);
  {
    httpd_req_t request; request.handle = server._server;
    request.user_ctx = &server; request.uri = "/ws/uart";
    request.authorization = valid;
    server._wsSessions.clear(); uart.accepts = false;
    assert(custom.ws_pre_handshake_cb(&request) == ESP_FAIL);
    assert(server._wsSessions.empty()); // no101 after custom refusal
    uart.accepts = true;
    assert(custom.ws_pre_handshake_cb(&request) == ESP_OK);
    assert(server._wsSessions.at(request.fd) == &uart);
    // If IDF's subsequent key/version validation or network handshake fails,
    // closeFn must release the already accepted custom physical resource once.
    server.wsSessionClosed(request.fd);
    server.wsSessionClosed(request.fd);
    assert(uart.closes == 1 && server._wsSessions.empty());
  }
  std::cout << "HTTP access: auth rejection, exact credentials, protected dispatch, query paths, methods and WS boundaries passed\n";
}
