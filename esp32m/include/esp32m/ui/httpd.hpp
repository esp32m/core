#include <esp_http_server.h>

#include "esp32m/ui/transport.hpp"

namespace esp32m {
  namespace ui {

    class Httpd : public Transport {
     public:
      Httpd();
      virtual ~Httpd();
      const char *name() const override {
        return "ui-http";
      };

     protected:
      void init(Ui *ui) override;
      esp_err_t wsSend(uint32_t cid, const char *text) override;

     private:
      httpd_config_t _config;
      httpd_handle_t _server;
      esp_err_t incomingReq(httpd_req_t *req);
      esp_err_t incomingWs(httpd_req_t *req);
      esp_err_t authenticate(httpd_req_t *req, bool &result);
      friend esp_err_t wsHandler(httpd_req_t *req);
      friend esp_err_t httpHandler(httpd_req_t *req);
      friend void closeFn(httpd_handle_t hd, int sockfd);
    };

  }  // namespace ui
}  // namespace esp32m