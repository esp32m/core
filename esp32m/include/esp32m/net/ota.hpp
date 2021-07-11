#pragma once

#include <mutex>

#include <esp_http_client.h>

#include "esp32m/app.hpp"

namespace esp32m {
  namespace net {
    class Ota : public AppObject {
     public:
      Ota(const Ota &) = delete;
      const char *name() const override;

      static Ota &instance();
      void setDefaultUrl(const char *url);

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(const JsonVariantConst args) override;
      bool handleRequest(Request &req) override;
      static const char *KeyOtaBegin;
      static const char *KeyOtaEnd;

     private:
      std::mutex *_mutex;
      char *_defaultUrl = nullptr;
      char *_url = nullptr;
      TaskHandle_t _task;
      esp_http_client_handle_t _httpClient = nullptr;
      bool _isUpdating = false;
      unsigned int _progress = 0, _total = 0;
      Ota();
      void run();
      void begin();
      void end();
      friend esp_err_t _http_client_init_cb(
          esp_http_client_handle_t http_client);
    };

    void useOta();

    namespace ota {
      extern const char *Name;
      void setDefaultUrl(const char *url);
    }  // namespace ota

  }  // namespace net
}  // namespace esp32m
