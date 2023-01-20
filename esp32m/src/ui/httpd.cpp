#include "esp32m/ui/httpd.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/net/ip_event.hpp"
#include "esp32m/net/mdns.hpp"
#include "esp32m/net/wifi.hpp"
#include "esp32m/ui.hpp"
#include "esp32m/ui/asset.hpp"
#include "esp32m/version.hpp"

#include <mdns.h>
#include <algorithm>
#include <vector>

namespace esp32m {
  namespace ui {
    const char *UriWs = "/ws";
    const char *UriRoot = "/";
    std::vector<Httpd *> _httpdServers;

    void freeNop(void *ctx){};

    void closeFn(httpd_handle_t hd, int sockfd) {
      Httpd *httpd = (Httpd *)httpd_get_global_user_ctx(hd);
      httpd->sessionClosed(sockfd);
      close(sockfd);
    }

    bool uriMatcher(const char *reference_uri, const char *uri_to_match,
                    size_t match_upto) {
      bool isWsRequest = !strcmp(uri_to_match, UriWs);
      if (!strcmp(reference_uri, UriWs))
        return isWsRequest;
      if (!strcmp(reference_uri, UriRoot))
        return !isWsRequest;
      logw("no handler for: %s, match: %s, size: %d", reference_uri,
           uri_to_match, match_upto);
      return false;
    }

    esp_err_t wsHandler(httpd_req_t *req) {
      Httpd *httpd = nullptr;
      for (Httpd *i : _httpdServers)
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

    esp_err_t httpHandler(httpd_req_t *req) {
      Httpd *httpd = (Httpd *)req->user_ctx;
      if (!httpd) {
        logw("no user_ctx: %s %d", req->uri, req->handle);
        return ESP_FAIL;
      }
      return httpd->incomingReq(req);
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

      net::mdns::Service *mdns = new net::mdns::Service("_http", "_tcp", 80);
      mdns->set("esp32m", ESP32M_VERSION);
      mdns->set("wsapi", "/ws");
      net::Mdns::instance().set(mdns);
      /*      EventManager::instance().subscribe([this](Event &ev) {
              if (mdns::EventPopulate::is(ev)) {
                mdns_txt_item_t serviceTxtData[2] = {
                    {"esp32m", ESP32M_VERSION},
                    {"wsapi", "/ws"},
                };
                mdns_service_remove("_http", "_tcp");
                ESP_ERROR_CHECK_WITHOUT_ABORT(mdns_service_add(
                    nullptr, "_http", "_tcp", 80, serviceTxtData, 2));
              }
            });*/
    }

    Httpd::~Httpd() {
      _httpdServers.erase(
          std::find(_httpdServers.begin(), _httpdServers.end(), this));
    }

    void Httpd::init(Ui *ui) {
      Transport::init(ui);
      if (ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_start(&_server, &_config)) ==
          ESP_OK) {
        httpd_uri_t uh = {.uri = UriWs,
                          .method = HTTP_GET,
                          .handler = wsHandler,
                          .user_ctx = this,
                          .is_websocket = true,
                          .handle_ws_control_frames = false,
                          .supported_subprotocol = 0};
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(_server, &uh));
        uh.uri = UriRoot;
        uh.is_websocket = false;
        uh.handler = httpHandler;
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_register_uri_handler(_server, &uh));
      }
    }

    esp_err_t Httpd::incomingReq(httpd_req_t *req) {
      // logI("uri: %s", req->uri);
      Asset *def = nullptr, *found = nullptr;
      for (Asset &a : _ui->assets()) {
        if (!strcmp(req->uri, a._uri)) {
          found = &a;
          break;
        }
        if (!strcmp(a._uri, "/"))
          def = &a;
      }
      if (!found) {
        if (!strcmp(req->uri, "/generate_204")) {
          char location[32];
          auto &info = net::Wifi::instance().apIp();
          sprintf(location, "http://" IPSTR "/cp", IP2STR(&info.ip));
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_status(req, "302"));  // 307 ???
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_hdr(req, "Location", location));
          // httpd_resp_set_type(req, "text/plain");
          ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send(req, nullptr, 0));
          logI("redirecting to captive portal %s", location);
          return ESP_OK;
        }
        found = def;
      }
      if (!found) {
        logI("404: %s", req->uri);
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send_404(req));
        return ESP_FAIL;
      }
      char buf[40];
      // logI("serving %s", req->uri);
      if (found->_etag &&
          httpd_req_get_hdr_value_str(req, "If-None-Match", buf, sizeof(buf)) ==
              ESP_OK &&
          !strcmp(buf, found->_etag)) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_set_status(req, "304"));
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send(req, nullptr, 0));
      } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(
            httpd_resp_set_type(req, found->_contentType));
        if (found->_contentEncoding)
          ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_set_hdr(
              req, "Content-Encoding", found->_contentEncoding));
        if (found->_etag) {
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_hdr(req, "ETag", found->_etag));
          ESP_ERROR_CHECK_WITHOUT_ABORT(
              httpd_resp_set_hdr(req, "Cache-Control", "no-cache"));
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(httpd_resp_send(
            req, (const char *)found->_start, found->_end - found->_start));
      }

      return ESP_OK;
    }

    esp_err_t Httpd::incomingWs(httpd_req_t *req) {
      if (req->method == HTTP_GET) {
        logI("WS client connected");
        return ESP_OK;
      }
      httpd_ws_frame_t ws_pkt;
      memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
      ESP_CHECK_RETURN(httpd_ws_recv_frame(req, &ws_pkt, 0));

      ws_pkt.payload = (uint8_t *)calloc(1, ws_pkt.len + 1);
      if (!ws_pkt.payload)
        return ESP_ERR_NO_MEM;
      esp_err_t ret = ESP_ERROR_CHECK_WITHOUT_ABORT(
          httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len));
      if (ret == ESP_OK)
        switch (ws_pkt.type) {
          case HTTPD_WS_TYPE_TEXT:
            incoming(httpd_req_to_sockfd(req), ws_pkt.payload, ws_pkt.len);
            break;
          default:
            logI("WS packet type %d was not handled", ws_pkt.type);
            break;
        }
      free(ws_pkt.payload);
      return ret;
    }

    esp_err_t Httpd::wsSend(uint32_t cid, const char *text) {
      httpd_ws_frame_t ws_pkt;
      memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
      ws_pkt.payload = (uint8_t *)text;
      ws_pkt.len = strlen(text);
      ws_pkt.type = HTTPD_WS_TYPE_TEXT;
      return httpd_ws_send_frame_async(_server, cid, &ws_pkt);
    }

  }  // namespace ui
}  // namespace esp32m
