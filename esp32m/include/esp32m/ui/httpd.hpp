#pragma once

#include <esp_http_server.h>

#include <list>
#include <map>
#include <mutex>
#include <string>

#include "esp32m/ui/transport.hpp"

namespace esp32m {
  namespace ui {

    class IWSHandler {
     public:
      explicit IWSHandler(const char *uri) : _uri(uri ? uri : "") {}
      virtual ~IWSHandler() = default;
      IWSHandler(const IWSHandler &) = delete;
      IWSHandler &operator=(const IWSHandler &) = delete;

      const char *uri() const {
        return _uri.c_str();
      }

      virtual esp_err_t handle(httpd_req_t *req) = 0;

      virtual void sessionClosed(int sockfd) {
        (void)sockfd;
      }

     private:
      std::string _uri;
    };

    class Httpd : public Transport {
     public:
      static Httpd *instance();
      virtual ~Httpd();
      const char *name() const override {
        return "ui-http";
      };

      /** Register an additional websocket endpoint served by this Httpd.
       * 
       * The default esp32m UI control channel uses `/ws` (TEXT frames with JSON).
       * This API is intended for extra endpoints (e.g. `/ws/uart` for binary data).
       */
      esp_err_t addWsHandler(IWSHandler &handler);

     protected:
      void init(Ui *ui) override;
      esp_err_t sendTo(uint32_t cid, const char *text) override;

     private:
      Httpd();
      httpd_config_t _config;
      httpd_handle_t _server = nullptr;
      std::list<IWSHandler *> _wsHandlers;
      std::mutex _wsSessionMutex;
      std::map<int, IWSHandler *> _wsSessions;
      esp_err_t incomingReq(httpd_req_t *req);
      esp_err_t incomingWs(httpd_req_t *req);
      esp_err_t authenticate(httpd_req_t *req, bool &result);
      void wsSessionClosed(int sockfd);
      static esp_err_t wsHandlerCustom(httpd_req_t *req);
      friend esp_err_t wsHandler(httpd_req_t *req);
      friend esp_err_t httpHandler(httpd_req_t *req);
      friend void closeFn(httpd_handle_t hd, int sockfd);
    };

  }  // namespace ui
}  // namespace esp32m