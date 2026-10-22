#include "esp32m/ui/httpd.hpp"
#include "esp32m/ui/httpd-access.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/net/net.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/net/mdns.hpp"
#include "esp32m/net/wifi.hpp"
#include "esp32m/ui.hpp"
#include "esp32m/ui/asset.hpp"
#include "esp32m/version.h"

#include <esp_tls_crypto.h>
#include <mdns.h>
#include <stdint.h>
#include <algorithm>
#include <vector>
#include <cstring>
#include "sdkconfig.h"

#define HTTPD_401 "401 UNAUTHORIZED" /*!< HTTP Response 401 */
namespace esp32m {
  namespace ui {
    const char* UriWs = "/ws";
    const char* UriRoot = "/";
    std::vector<Httpd*> _httpdServers;

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define HTTPD_SCRATCH_BUF \
  MAX(CONFIG_HTTPD_MAX_REQ_HDR_LEN, CONFIG_HTTPD_MAX_URI_LEN)
    struct httpd_req_aux {
      struct sock_db* sd; /*!< Pointer to socket database */
      char scratch[HTTPD_SCRATCH_BUF +
                   1]; /*!< Temporary buffer for our operations (1 byte extra
                          for null termination) */
      size_t remaining_len;     /*!< Amount of data remaining to be fetched */
      char* status;             /*!< HTTP response's status code */
      char* content_type;       /*!< HTTP response's content type */
      bool first_chunk_sent;    /*!< Used to indicate if first chunk sent */
      unsigned req_hdrs_count;  /*!< Count of total headers in request packet */
      unsigned resp_hdrs_count; /*!< Count of additional headers in response
                                   packet */
      struct resp_hdr {
        const char* field;
        const char* value;
      }* resp_hdrs; /*!< Additional headers in response packet */
      struct http_parser_url url_parse_res; /*!< URL parsing result, used for
                                               retrieving URL elements */
#ifdef CONFIG_HTTPD_WS_SUPPORT
      bool ws_handshake_detect; /*!< WebSocket handshake detection flag */
      httpd_ws_type_t ws_type;  /*!< WebSocket frame type */
      bool ws_final;            /*!< WebSocket FIN bit (final frame or not) */
      uint8_t mask_key[4];      /*!< WebSocket mask key for this payload */
#endif
    };

    esp_err_t dumpHeaders(httpd_req_t* r) {
      if (r == NULL)
        return ESP_ERR_INVALID_ARG;

      auto ra = (struct httpd_req_aux*)r->aux;
      const char* hdr_ptr = ra->scratch;
      unsigned count = ra->req_hdrs_count;

      while (count--) {
        const char* val_ptr = strchr(hdr_ptr, ':');
        if (!val_ptr)
          break;
        logi("header: %s", hdr_ptr);

        if (count) {
          hdr_ptr = 1 + strchr(hdr_ptr, '\0');
          while (*hdr_ptr == '\0') hdr_ptr++;
        }
      }
      return ESP_OK;
    }

    void freeNop(void* ctx) {};

    void closeFn(httpd_handle_t hd, int sockfd) {
      Httpd* httpd = (Httpd*)httpd_get_global_user_ctx(hd);
      httpd->wsSessionClosed(sockfd);
      httpd->sessionClosed(sockfd);
      close(sockfd);
    }

    static inline bool isWsPath(const char* uri) {
      return http::isWebSocketPath(uri);
    }

    bool uriMatcher(const char* reference_uri, const char* uri_to_match,
                    size_t match_upto) {
      (void)match_upto;
      return http::uriMatches(reference_uri, uri_to_match);
    }

    esp_err_t wsHandler(httpd_req_t* req) {
      Httpd* httpd = nullptr;
      for (Httpd* i : _httpdServers)
        if (i->_server == req->handle) {
          httpd = i;
          break;
        }
      if (!httpd) {
        logw("no user_ctx: %s %d", req->uri, req->handle);
        return ESP_FAIL;
      }
      return httpd->incomingWs(req);
    }

    esp_err_t Httpd::httpHandlerCustom(httpd_req_t* req) {
      Httpd* httpd = nullptr;
      for (Httpd* i : _httpdServers)
        if (i->_server == req->handle) {
          httpd = i;
          break;
        }
      if (!httpd) {
        logw("httpHandlerCustom: no httpd for handle");
        return ESP_FAIL;
      }
      bool accessGranted = false;
      ESP_CHECK_RETURN(httpd->authenticate(req, accessGranted));
      if (!accessGranted)
        return ESP_OK;
      for (auto* h : httpd->_httpHandlers)
        if (h && http::handlerMatches(h->uri(), h->method(), req->uri,
                                     req->method))
          return h->handle(req);
      logw("httpHandlerCustom: no handler for %s", req->uri);
      return ESP_ERR_NOT_FOUND;
    }

    esp_err_t Httpd::wsHandlerCustom(httpd_req_t* req) {
      Httpd* httpd = nullptr;
      for (Httpd* i : _httpdServers)
        if (i->_server == req->handle) {
          httpd = i;
          break;
        }
      if (!httpd) {
        logw("no user_ctx: %s %d", req->uri, req->handle);
        return ESP_FAIL;
      }

      const int fd = httpd_req_to_sockfd(req);

      // Enforce the same auth gate as /ws on the websocket handshake.
      if (req->method == HTTP_GET) {
        bool accessGranted = false;
        ESP_CHECK_RETURN(httpd->authenticate(req, accessGranted));
        if (!accessGranted)
          return ESP_OK;
        // Let the custom handler decide whether to accept or reject.
      }

      // On handshake we can reliably route by URI and remember the handler.
      if (req->method == HTTP_GET) {
        for (auto* h : httpd->_wsHandlers) {
          if (h && req->uri[0] && http::pathMatches(h->uri(), req->uri)) {
            const esp_err_t err = h->handle(req);
            if (err == ESP_OK) {
              std::lock_guard<std::mutex> guard(httpd->_wsSessionMutex);
              httpd->_wsSessions[fd] = h;
            }
            return err;
          }
        }
        logw("no ws handler for: %s", req->uri[0] ? req->uri : "(empty)");
        return ESP_ERR_NOT_FOUND;
      }

      // For data frames, route by sockfd (req->uri may be empty/unstable).
      {
        std::lock_guard<std::mutex> guard(httpd->_wsSessionMutex);
        auto it = httpd->_wsSessions.find(fd);
        if (it != httpd->_wsSessions.end() && it->second) {
          return it->second->handle(req);
        }
      }

      // Fallback: try URI match if available.
      for (auto* h : httpd->_wsHandlers) {
        if (h && req->uri[0] && http::pathMatches(h->uri(), req->uri)) {
          return h->handle(req);
        }
      }
      logw("no ws handler for fd=%d uri=%s", fd, req->uri[0] ? req->uri : "(empty)");
      return ESP_ERR_NOT_FOUND;
    }

    void Httpd::wsSessionClosed(int sockfd) {
      IWSHandler* handler = nullptr;
      {
        std::lock_guard<std::mutex> guard(_wsSessionMutex);
        _controlSessions.erase(sockfd);
        auto it = _wsSessions.find(sockfd);
        if (it != _wsSessions.end()) {
          handler = it->second;
          _wsSessions.erase(it);
        }
      }
      if (handler)
        handler->sessionClosed(sockfd);
    }

    esp_err_t httpHandler(httpd_req_t* req) {
      Httpd* httpd = (Httpd*)req->user_ctx;
      if (!httpd) {
        logw("no user_ctx: %s %d", req->uri, req->handle);
        return ESP_FAIL;
      }
      return httpd->incomingReq(req);
    }

    static char* http_auth_basic(const char* username, const char* password) {
      size_t out;
      char* user_info = NULL;
      char* digest = NULL;
      size_t n = 0;
      const auto usernameLength = strlen(username);
      const auto passwordLength = strlen(password);
      if (passwordLength > SIZE_MAX - 2 ||
          usernameLength > SIZE_MAX - passwordLength - 2)
        return NULL;
      const auto credentialsLength = usernameLength + passwordLength + 1;
      user_info = (char*)malloc(credentialsLength + 1);
      if (!user_info)
        return NULL;
      memcpy(user_info, username, usernameLength);
      user_info[usernameLength] = ':';
      memcpy(user_info + usernameLength + 1, password, passwordLength);
      user_info[credentialsLength] = 0;
      esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char*)user_info,
                               credentialsLength);
      if (!n || n > SIZE_MAX - 7) {
        free(user_info);
        return NULL;
      }

      /* 6: The length of the "Basic " string
       * n: Number of bytes for a base64 encode format
       * 1: Number of bytes for a reserved which be used to fill zero
       */
      digest = (char*)calloc(1, 6 + n + 1);
      if (digest) {
        strcpy(digest, "Basic ");
        if (esp_crypto_base64_encode((unsigned char*)digest + 6, n, &out,
                                     (const unsigned char*)user_info,
                                     credentialsLength) != 0 || out > n) {
          free(digest);
          digest = NULL;
        }
      }
      free(user_info);
      return digest;
    }

    esp_err_t getHeader(httpd_req_t* req, const char* name,
                        std::string& value) {
      auto len = httpd_req_get_hdr_value_len(req, name);
      if (len) {
        size_t bufsize = len + 1;
        auto buf = (char*)malloc(bufsize);
        if (!buf)
          return ESP_ERR_NO_MEM;
        auto res = httpd_req_get_hdr_value_str(req, name, buf, bufsize);
        if (res == ESP_OK)
          value = buf;
        free(buf);
        return res;
      }
      return ESP_ERR_NOT_FOUND;
    }

    Httpd::Httpd() {
      _config = HTTPD_DEFAULT_CONFIG();
      _config.global_user_ctx = this;
      _config.global_user_ctx_free_fn = freeNop;
      _config.uri_match_fn = uriMatcher;
      _config.close_fn = closeFn;
      _config.lru_purge_enable = true;
      // esp_log_level_set("httpd_ws", ESP_LOG_DEBUG);
      _httpdServers.push_back(this);

      net::mdns::Service* mdns = new net::mdns::Service("_http", "_tcp", 80);
      mdns->set("esp32m", ESP32M_VERSION);
      mdns->set("wsapi", "/ws");
      net::Mdns::instance().set(mdns);
    }

    Httpd *Httpd::instance() {
      static Httpd *i = new Httpd();
      return i;
    }

    Httpd::~Httpd() {
      _httpdServers.erase(
          std::find(_httpdServers.begin(), _httpdServers.end(), this));
    }

    void Httpd::init(Ui* ui) {
      Transport::init(ui);
      net::useNetif();
      if (ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_start(&_server, &_config)) ==
          ESP_OK) {
        // Register WebSocket handler
        httpd_uri_t uh = {};
        uh.uri = UriWs;
        uh.method = HTTP_GET;
        uh.handler = wsHandler;
        uh.user_ctx = this;
        uh.is_websocket = true;
        prepareWsHandler(uh);
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(_server, &uh));

#ifdef CONFIG_HTTPD_WS_SUPPORT
        // Register additional websocket endpoints provided by modules.
        for (auto* e : _wsHandlers) {
          if (!e)
            continue;
          httpd_uri_t wh = {};
          wh.uri = e->uri();
          wh.method = HTTP_GET;
          wh.handler = Httpd::wsHandlerCustom;
          wh.user_ctx = this;
          wh.is_websocket = true;
          prepareWsHandler(wh);
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_register_uri_handler(_server, &wh));
        }
#endif

        // Register custom HTTP handlers before root so exact URIs take priority
        for (auto* e : _httpHandlers) {
          if (!e)
            continue;
          httpd_uri_t hh = {};
          hh.uri = e->uri();
          hh.method = e->method();
          hh.handler = Httpd::httpHandlerCustom;
          hh.user_ctx = this;
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_register_uri_handler(_server, &hh));
        }

        // Root handler registered last so it acts as a catch-all
        uh.uri = UriRoot;
        uh.is_websocket = false;
        uh.handler = httpHandler;
#if CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
        uh.ws_pre_handshake_cb = nullptr;
#endif
#if CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
        uh.ws_post_handshake_cb = nullptr;
#endif
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(_server, &uh));
      }
    }

    esp_err_t Httpd::addWsHandler(IWSHandler& handler) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
      (void)handler;
      return ESP_ERR_NOT_SUPPORTED;
#else
      const char* uri = handler.uri();
      if (!uri || !*uri)
        return ESP_ERR_INVALID_ARG;
      if (!isWsPath(uri))
        return ESP_ERR_INVALID_ARG;
      if (!strcmp(uri, UriWs))
        return ESP_ERR_INVALID_ARG;

      for (auto* e : _wsHandlers)
        if (e && e->uri() && !strcmp(e->uri(), uri))
          return ESP_ERR_INVALID_STATE;

      _wsHandlers.push_back(&handler);

      // If server is already running, register immediately.
      if (_server) {
        auto* e = _wsHandlers.back();
        if (!e)
          return ESP_ERR_INVALID_STATE;
        httpd_uri_t wh = {};
        wh.uri = e->uri();
        wh.method = HTTP_GET;
        wh.handler = Httpd::wsHandlerCustom;
        wh.user_ctx = this;
        wh.is_websocket = true;
        prepareWsHandler(wh);
        return httpd_register_uri_handler(_server, &wh);
      }
      return ESP_OK;
#endif
    }

    esp_err_t Httpd::addHttpHandler(IHttpHandler& handler) {
      _httpHandlers.push_back(&handler);
      if (_server) {
        // Unregister root catch-all so the new exact handler takes priority
        httpd_unregister_uri_handler(_server, UriRoot, HTTP_GET);
        httpd_uri_t hh = {};
        hh.uri = handler.uri();
        hh.method = handler.method();
        hh.handler = Httpd::httpHandlerCustom;
        hh.user_ctx = this;
        ESP_CHECK_RETURN(httpd_register_uri_handler(_server, &hh));
        // Re-register root handler last
        httpd_uri_t rootUh = {};
        rootUh.uri = UriRoot;
        rootUh.method = HTTP_GET;
        rootUh.handler = httpHandler;
        rootUh.user_ctx = this;
        ESP_CHECK_RETURN(httpd_register_uri_handler(_server, &rootUh));
      }
      return ESP_OK;
    }

    esp_err_t Httpd::incomingReq(httpd_req_t* req) {
      // logI("uri: %s", req->uri);
      // dumpHeaders(req);
      if (!req)
        return ESP_OK;

      bool accessGranted = false;
      ESP_CHECK_RETURN(authenticate(req, accessGranted));
      if (!accessGranted)
        return ESP_OK;

      auto cp = false;
      if (!strcmp(req->uri, "/generate_204") ||
          !strcmp(req->uri, "/hotspot-detect.html")) {
        cp = true;
      } else {
        std::string hv;
        if (getHeader(req, "Host", hv) == ESP_OK) {
          // logI("host %s", hv.c_str());
          if (hv == "captive.apple.com" || hv == "detectportal.firefox.com" ||
              hv == "www.msftconnecttest.com")
            cp = true;
        }
      }
      if (cp) {
        net::wifi::Ap* ap = net::Wifi::instance().ap();
        if (ap) {
          char location[32];
          esp_netif_ip_info_t info;
          ap->getIpInfo(info);
          sprintf(location, "http://" IPSTR "/cp", IP2STR(&info.ip));
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_status(req, "302"));  // 307 ???
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_hdr(req, "Location", location));
          // httpd_resp_set_type(req, "text/plain");
          ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send(req, nullptr, 0));
          // logI("redirecting to captive portal %s", location); // this line
          // causes havoc on esp32c6, esp-idf 5.2.1
          logI("redirecting to captive portal");
          return ESP_OK;
        } else
          logW("got %s request while AP is not running", req->uri);
      }

      Asset *def = nullptr, *found = nullptr;
      for (auto& a : _ui->assets()) {
        auto& info = a->info();
        if (!strcmp(req->uri, info.uri)) {
          found = a.get();
          break;
        }
        if (!strcmp(info.uri, "/"))
          def = a.get();
      }
      if (!found) {
        if (!strEndsWith(req->uri, ".ico"))
          found = def;
        /*logI("strEndsWith %s %s %d %d", req->uri, ".js.map",
             strEndsWith(req->uri, ".js.map"), found == nullptr);*/
      }
      if (!found) {
        logI("404: %s", req->uri);
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send_404(req));
        return ESP_OK;
      }
      char buf[40];
      auto& info = found->info();
      // logI("serving %s", req->uri);
      if (info.etag &&
          httpd_req_get_hdr_value_str(req, "If-None-Match", buf, sizeof(buf)) ==
              ESP_OK &&
          !strcmp(buf, info.etag)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_set_status(req, "304"));
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send(req, nullptr, 0));
      } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            httpd_resp_set_type(req, info.contentType));
        if (info.contentEncoding)
          ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_set_hdr(
              req, "Content-Encoding", info.contentEncoding));
        if (info.etag) {
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_hdr(req, "ETag", info.etag));
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_hdr(req, "Cache-Control", "no-cache"));
        }
        switch (found->type()) {
          case AssetType::Memory: {
            auto memAsset = static_cast<MemoryAsset*>(found);
            ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send(
                req, (const char*)memAsset->data(), memAsset->size()));
            break;
          }
          case AssetType::Readable: {
            auto reader = static_cast<ReadableAsset*>(found)->createReader();
            if (reader) {
              const size_t bufSize = 4096;
              uint8_t* buffer = new uint8_t[bufSize];
              size_t read = 0;
              while ((read = reader->read(buffer, bufSize)) > 0 &&
                     ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send_chunk(
                         req, (const char*)buffer, read)) == ESP_OK) {
              }
              ESP_ERROR_CHECK_WITHOUT_ABORT(
                  httpd_resp_send_chunk(req, (const char*)buffer, 0));
              delete[] buffer;
            }
          }
          default:
            break;
        }
      }

      return ESP_OK;
    }

    esp_err_t Httpd::wsPreHandshake(httpd_req_t* req) {
      auto* httpd = static_cast<Httpd*>(req->user_ctx);
      if (!httpd)
        return ESP_ERR_INVALID_STATE;
      bool accessGranted = false;
      ESP_CHECK_RETURN(httpd->authenticate(req, accessGranted));
      // authenticate() has already sent the 401 response. Returning an error
      // stops IDF from upgrading this unauthorized HTTP request to WebSocket.
      if (!accessGranted)
        return ESP_FAIL;
      // Custom endpoints may reject (e.g. UART not armed/busy) with an HTTP
      // response, so their acceptance and FD routing must precede the upgrade.
      return http::pathMatches("/ws", req->uri) ? ESP_OK : wsHandlerCustom(req);
    }

    esp_err_t Httpd::wsPostHandshake(httpd_req_t* req) {
      auto* httpd = static_cast<Httpd*>(req->user_ctx);
      if (!httpd)
        return ESP_ERR_INVALID_STATE;
      // PRE has already accepted/authenticated this HTTP request. Do not
      // reauthenticate or send another HTTP response after IDF has sent101.
      auto cid = httpd_req_to_sockfd(req);
      auto generation = httpd->sessionOpened(cid);
      std::lock_guard<std::mutex> lock(httpd->_wsSessionMutex);
      httpd->_controlSessions[cid] = generation;
      return ESP_OK;
    }

    void Httpd::prepareWsHandler(httpd_uri_t& handler) {
#if CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
      handler.ws_pre_handshake_cb = Httpd::wsPreHandshake;
#endif
#if CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
      // ESP-IDF 6.1 skips uri->handler for an HTTP upgrade. Invoke that same
      // control-channel handshake path after upgrade to establish accepted
      // session generation before the first data frame. Custom endpoint
      // acceptance has already run in PRE so it can still return HTTP errors.
      handler.ws_post_handshake_cb = handler.handler == Httpd::wsHandlerCustom
                                        ? nullptr : Httpd::wsPostHandshake;
#endif
    }

    esp_err_t Httpd::incomingWs(httpd_req_t* req) {
      if (req->method == HTTP_GET) {
        bool accessGranted = false;
        ESP_CHECK_RETURN(authenticate(req, accessGranted));
        if (!accessGranted)
          return ESP_OK;
        auto cid = httpd_req_to_sockfd(req);
        auto generation = sessionOpened(cid);
        {
          std::lock_guard<std::mutex> lock(_wsSessionMutex);
          _controlSessions[cid] = generation;
        }
        return ESP_OK;
      }
      httpd_ws_frame_t ws_pkt;
      memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
      ESP_CHECK_RETURN(httpd_ws_recv_frame(req, &ws_pkt, 0));
      auto generation = sessionGeneration(httpd_req_to_sockfd(req));
      // Bound allocation before reading the frame or parsing its JSON.
      if (ws_pkt.len > 16 * 1024 ||
          !generation)
        return ESP_ERR_INVALID_SIZE;

      ws_pkt.payload = (uint8_t*)calloc(1, ws_pkt.len + 1);
      if (!ws_pkt.payload)
        return ESP_ERR_NO_MEM;
      esp_err_t ret = ESP_ERROR_CHECK_WITHOUT_ABORT(
          httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len));
      if (ret == ESP_OK)
        switch (ws_pkt.type) {
          case HTTPD_WS_TYPE_TEXT:
            incoming(httpd_req_to_sockfd(req), ws_pkt.payload, ws_pkt.len,
                     generation);
            break;
          default:
            logI("WS packet type %d was not handled", ws_pkt.type);
            break;
        }
      free(ws_pkt.payload);
      return ret;
    }

    esp_err_t Httpd::authenticate(httpd_req_t* req, bool& result) {
      result = false;
      std::string username, password;
#ifdef CONFIG_ESP32M_UI_HTTPD_BASIC_AUTH_USERNAME
      username = CONFIG_ESP32M_UI_HTTPD_BASIC_AUTH_USERNAME;
#endif
#ifdef CONFIG_ESP32M_UI_HTTPD_BASIC_AUTH_PASSWORD
      password = CONFIG_ESP32M_UI_HTTPD_BASIC_AUTH_PASSWORD;
#endif

      auto& auth = _ui->auth();
      if (auth.enabled) {
        username = auth.username;
        password = auth.password;
      }

      if (username.size() && password.size()) {
        const auto headerLength =
            httpd_req_get_hdr_value_len(req, "Authorization");
        if (headerLength) {
          char* buf = (char*)calloc(1, headerLength + 1);
          if (!buf)
            return ESP_ERR_NO_MEM;
          if (httpd_req_get_hdr_value_str(req, "Authorization", buf,
                                          headerLength + 1) ==
              ESP_OK) {
            char* expected =
                http_auth_basic(username.c_str(), password.c_str());
            if (!expected) {
              free(buf);
              return ESP_ERR_NO_MEM;
            }
            result = http::basicAuthorizationMatches(expected, buf,
                                                      headerLength);
            free(expected);
          }
          free(buf);
        }
        if (!result) {
          ESP_CHECK_RETURN(httpd_resp_set_status(req, HTTPD_401));
          ESP_CHECK_RETURN(httpd_resp_set_hdr(req, "Connection", "keep-alive"));
          ESP_CHECK_RETURN(httpd_resp_set_hdr(req, "WWW-Authenticate",
                                             "Basic realm=\"ESP32M\""));
          ESP_CHECK_RETURN(httpd_resp_send(req, NULL, 0));
        }
      } else
        result = true;
      return ESP_OK;
    }

    esp_err_t Httpd::sendTo(uint32_t cid, const char* text) {
      return sendSession(cid, sessionGeneration(cid),
                         std::make_shared<const std::string>(text ? text : ""));
    }

    void Httpd::closeSession(uint32_t cid, uint64_t generation) {
      if (!sessionAlive(cid, generation))
        return;
      Transport::closeSession(cid, generation);
      {
        std::lock_guard<std::mutex> lock(_sendMutex);
        ++_slowClosed;
      }
      struct CloseWork { Httpd* owner; uint32_t cid; uint64_t generation; };
      auto work = new (std::nothrow) CloseWork{this, cid, generation};
      auto shutdownCurrent = [&] {
        // closeFn erases this map before closing/releasing the physical FD.
        // Holding this lock therefore also makes the failure fallback safe.
        std::lock_guard<std::mutex> lock(_wsSessionMutex);
        auto i = _controlSessions.find(cid);
        if (i != _controlSessions.end() && i->second == generation)
          shutdown(cid, SHUT_RDWR);
      };
      if (!work) {
        shutdownCurrent();
        return;
      }
      // Do not queue IDF's sock_db pointer: it may be reused before the work
      // executes. Verify the handshake generation on the HTTPD task itself.
      auto err = _server ? httpd_queue_work(_server, [](void* argument) {
        std::unique_ptr<CloseWork> item((CloseWork*)argument);
        auto* owner = item->owner;
        std::lock_guard<std::mutex> lock(owner->_wsSessionMutex);
        auto i = owner->_controlSessions.find(item->cid);
        if (i != owner->_controlSessions.end() && i->second == item->generation)
          shutdown(item->cid, SHUT_RDWR);
      }, work) : ESP_ERR_INVALID_STATE;
      if (err != ESP_OK) {
        delete work;
        shutdownCurrent();
      }
    }

    void Httpd::sendStats(JsonObject result) {
      std::lock_guard<std::mutex> lock(_sendMutex);
      result["queuedBytes"] = _queuedBytes;
      result["queuedWorks"] = _queuedWorks;
      result["byteLimit"] = 32 * 1024;
      result["workLimit"] = 32;
      result["rejected"] = _sendRejected;
      result["failed"] = _sendFailed;
      result["closed"] = _slowClosed;
    }

    esp_err_t Httpd::sendSession(uint32_t cid, uint64_t generation,
                                std::shared_ptr<const std::string> text) {
      if (!_server || !text || !sessionAlive(cid, generation))
        return ESP_ERR_INVALID_STATE;

      struct WorkItem {
        Httpd* owner;
        httpd_handle_t server;
        uint32_t fd;
        uint64_t generation;
        std::shared_ptr<const std::string> payload;
        size_t charge;
      };
      // Conservative charging includes string capacity and work metadata for
      // every destination, even when the immutable body is physically shared.
      size_t charge = sizeof(WorkItem) + text->capacity() + 1 + 64;
      bool admitted;
      {
        std::lock_guard<std::mutex> lock(_sendMutex);
        admitted = charge <= 32 * 1024 - std::min<size_t>(_queuedBytes, 32 * 1024) &&
                   _queuedWorks < 32;
        if (admitted) {
          _queuedBytes += charge;
          ++_queuedWorks;
        } else {
          ++_sendRejected;
        }
      }
      if (!admitted) {
        closeSession(cid, generation);
        return ESP_ERR_NO_MEM;
      }
      auto work = new (std::nothrow) WorkItem{this, _server, cid, generation,
                                            std::move(text), charge};
      if (!work) {
        {
          std::lock_guard<std::mutex> lock(_sendMutex);
          _queuedBytes -= charge;
          --_queuedWorks;
          ++_sendRejected;
        }
        closeSession(cid, generation);
        return ESP_ERR_NO_MEM;
      }
      auto fn = [](void* arg) {
        std::unique_ptr<WorkItem> w((WorkItem*)arg);
        auto* owner = w->owner;
        bool failed = false;
        // HTTPD processes this check and actual socket send on its own task:
        // a reused FD cannot receive a predecessor's queued output.
        if (owner->sessionAlive(w->fd, w->generation)) {
          httpd_ws_frame_t packet = {};
          packet.payload = (uint8_t*)w->payload->data();
          packet.len = w->payload->size();
          packet.type = HTTPD_WS_TYPE_TEXT;
          failed = httpd_ws_send_frame_async(w->server, w->fd, &packet) != ESP_OK;
        }
        {
          std::lock_guard<std::mutex> lock(owner->_sendMutex);
          owner->_queuedBytes -= w->charge;
          --owner->_queuedWorks;
          if (failed)
            ++owner->_sendFailed;
        }
        if (failed)
          owner->closeSession(w->fd, w->generation);
      };
      auto err = httpd_queue_work(_server, fn, work);
      if (err != ESP_OK) {
        {
          std::lock_guard<std::mutex> lock(_sendMutex);
          _queuedBytes -= work->charge;
          --_queuedWorks;
          ++_sendRejected;
        }
        delete work;
        closeSession(cid, generation);
        return err;
      }
      return ESP_OK;
    }

  }  // namespace ui
}  // namespace esp32m
