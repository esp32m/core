#pragma once

#include "esp32m/app.hpp"

#include <sdkconfig.h>
#include <mutex>
#include <string>

#include <esp_http_client.h>
#include <esp_http_server.h>

namespace esp32m {
  namespace net {
    namespace ota {

       constexpr const char * ErrNoUrl = "ERR_NET_OTA_NO_URL";

      union Features {
        struct {
          bool checkForUpdates : 1;
          /** If set, the user is allowed to burn only the firmware downloaded
             from the vendor's server */
          bool vendorOnly : 1;
          /** If set, the user may upload a binary directly from the browser */
          bool fileUpload : 1;
        };
        int value;
      };

      union StateFlags {
        struct {
          bool updating : 1;
          bool checking : 1;
          bool newver : 1;
        };
        int value;
      };

      extern const char *Name;
      bool isRunning();

    }  // namespace ota

    class Ota : public AppObject {
     public:
      Ota(const Ota &) = delete;
      const char *name() const override;
      ota::Features features() const;
      ota::StateFlags flags();

      static Ota &instance();
      esp_err_t handleUpload(httpd_req_t *req);

     protected:
      JsonDocument *getState(RequestContext &ctx) override;
      bool setConfig(RequestContext &ctx) override;
      JsonDocument *getConfig(RequestContext &ctx) override;
      bool handleRequest(Request &req) override;
      static const char *KeyOtaBegin;
      static const char *KeyOtaEnd;

     private:
      std::mutex *_mutex;
      std::string _savedUrl, _pendingUrl;
      TaskHandle_t _task;
      bool _updating = false;
      esp_http_client_handle_t _httpClient = nullptr;
      unsigned int _progress = 0, _total = 0;
      httpd_req_t *_asyncReq = nullptr;
      Ota();
      void run();
      void begin();
      void end();
      void perform(const char *url);
      void performUploadAsync();

      friend esp_err_t _http_client_init_cb(
          esp_http_client_handle_t http_client);
    };

    void useOta();

  }  // namespace net
}  // namespace esp32m
