#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>

#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/config/changed.hpp"
#include "esp32m/events.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/json.hpp"

#include "esp32m/net/ota.hpp"

namespace esp32m {
  namespace net {

    namespace ota {
      const char *Name = "ota";
      bool _isRunning = false;
      
      void setDefaultUrl(const char *url) {
        Ota::instance().setDefaultUrl(url);
      }

      bool isRunning() {
        return _isRunning;
      }
    }  // namespace ota

    const char *Ota::KeyOtaBegin = "begin";
    const char *Ota::KeyOtaEnd = "end";

    Ota::Ota() {
      _mutex = &locks::get(ota::Name);
      xTaskCreate([](void *self) { ((Ota *)self)->run(); }, "m/ota", 4096, this,
                  tskIDLE_PRIORITY + 1, &_task);  // 2kb stack is not enough
    }

    const char *Ota::name() const {
      return ota::Name;
    };

    DynamicJsonDocument *Ota::getState(const JsonVariantConst args) {
      auto doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(3));
      auto cr = doc->to<JsonObject>();
      if (_isUpdating) {
        cr["method"] = _url ? "http" : "udp";
        cr["progress"] = _progress;
        cr["total"] = _total;
      }
      return doc;
    }

    DynamicJsonDocument *Ota::getConfig(const JsonVariantConst args) {
      auto dl = _defaultUrl ? strlen(_defaultUrl) + 1 : 0;
      auto doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1) + dl);
      auto cr = doc->to<JsonObject>();
      if (dl)
        cr["url"] = _defaultUrl;
      return doc;
    }

    bool Ota::setConfig(const JsonVariantConst cfg,
                        DynamicJsonDocument **result) {
      bool changed = false;
      json::fromDup(cfg["url"], _defaultUrl, nullptr, &changed);
      return changed;
    }

    bool Ota::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("update")) {
        if (setConfig(req.data(), nullptr))
          EventConfigChanged::publish(this);
        _url = strdup(_defaultUrl);
        xTaskNotifyGive(_task);
        req.respond();
      } else
        return false;
      return true;
    }

    void Ota::setDefaultUrl(const char *url) {
      if (_defaultUrl) {
        free(_defaultUrl);
        _defaultUrl = nullptr;
      }
      if (url) {
        auto dl = strlen(url);
        if (dl) {
          if (url[dl - 1] != '/')
            _defaultUrl = strdup(url);
          else {
            auto name = App::instance().name();
            _defaultUrl = (char *)malloc(dl + 13 + strlen(name) + 1);
            sprintf(_defaultUrl, "%sfirmware/%s.bin", url, name);
          }
        }
      }
      if (_defaultUrl)
        logI("default OTA url: %s", _defaultUrl);
    }

    esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client) {
      Ota::instance()._httpClient = http_client;
      return ESP_OK;
    }

    void Ota::run() {
      esp_task_wdt_add(NULL);
      for (;;) {
        if (_url) {
          char *u = _url;
          begin();
          logI("http updater starting for %s", u);
          esp_http_client_config_t config = {};
          config.url = u;
          config.timeout_ms = 60 * 1000;
          config.skip_cert_common_name_check = true;
          esp_https_ota_config_t ota_config = {
              .http_config = &config,
              .http_client_init_cb =
                  _http_client_init_cb,  // Register a callback to be invoked
                                         // after esp_http_client is initialized
              .bulk_flash_erase = false,
              .partial_http_download = false,
              .max_http_request_size = 0};
          esp_https_ota_handle_t https_ota_handle = NULL;
          esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
          if (ESP_ERROR_CHECK_WITHOUT_ABORT(err) == ESP_OK) {
            while (1) {
              err = esp_https_ota_perform(https_ota_handle);
              if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
                break;
              if (!_total || _httpClient)
                _total = esp_http_client_get_content_length(_httpClient);
              _progress = esp_https_ota_get_image_len_read(https_ota_handle);
              esp_task_wdt_reset();
            }
            if (err == ESP_OK)
              err = esp_https_ota_finish(https_ota_handle);
          }
          end();
          free(u);
          if (err == ESP_OK) {
            logI("OTA update was successful, rebooting...");
            delay(1000);
            esp_task_wdt_reset();
            App::restart();
          }
        }
        esp_task_wdt_reset();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_isUpdating ? 1 : 1000));
      }
    }

    void Ota::begin() {
      if (App::instance().wdtTimeout()) {
        esp_task_wdt_config_t wdtc = {
            .timeout_ms = 60 * 3 * 1000,
            .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
            .trigger_panic = true};
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_task_wdt_reconfigure(&wdtc));
      }
      _isUpdating = true;
      ota::_isRunning=true;
      Broadcast::publish(name(), KeyOtaBegin);
      _mutex->lock();
    }

    void Ota::end() {
      Broadcast::publish(name(), KeyOtaEnd);
      _url = nullptr;
      _httpClient = nullptr;
      _progress = 0;
      _total = 0;
      _mutex->unlock();
      _isUpdating = false;
      ota::_isRunning=false;
      auto wt = App::instance().wdtTimeout();
      if (wt) {
        esp_task_wdt_config_t wdtc = {
            .timeout_ms = wt * 1000,
            .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
            .trigger_panic = true};
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_task_wdt_reconfigure(&wdtc));
      }
    }

    Ota &Ota::instance() {
      static Ota i;
      return i;
    }

    void useOta() {
      Ota::instance();
    }

  }  // namespace net
}  // namespace esp32m