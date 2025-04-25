#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>

#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/events.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/json.hpp"
#include "esp32m/net/http.hpp"
#include "esp32m/net/net.hpp"
#include "esp32m/net/ota.hpp"
#include "esp32m/resources.hpp"
#include "esp32m/url.hpp"
#include "esp32m/version.hpp"

#if CONFIG_ESP32M_NET_OTA_CHECK_FOR_UPDATES
#  ifndef CONFIG_ESP32M_NET_OTA_VENDOR_URL
#    error \
        "automated checking for updates is turned on, but firmware URL is not specified"
#  endif
#endif

namespace esp32m {
  namespace net {

    namespace ota {
      const char *Name = "ota";
      bool _isRunning = false;

      bool isRunning() {
        return _isRunning;
      }

#if CONFIG_ESP32M_NET_OTA_CHECK_FOR_UPDATES
      struct VersionInfo {
        Version version;
        std::string url;
      };

      class Check : public AppObject {
       public:
        const char *name() const override {
          return "ota-check";
        }
        static Check &instance() {
          static Check i;
          return i;
        }

        std::unique_ptr<ota::VersionInfo> newVersion;
        void run(string &url) {
          if (shouldCheckForUpdates()) {
            _running = true;
            checkForUpdates();
            _running = false;
          }
          auto now = millis();
          if (_autoUpdate && url.empty() && newVersion && now > 60 * 1000 &&
              (_lastUpdateAttempt == 0 ||
               now - _lastUpdateAttempt > 60 * 60 * 1000)) {
            url = newVersion->url;
            _lastUpdateAttempt = now;
            logI("auto updating from %s", url.c_str());
          }
        }

        void getFeatures(Features &f) {
          if (!_vendorUrl.empty())
            f.checkForUpdates = true;
#  if CONFIG_ESP32M_NET_OTA_VENDOR_ONLY
          f.vendorOnly = true;
#  endif
        }
        void getStateFlags(StateFlags &f) {
          f.checking = _running;
          if (newVersion)
            f.newver = true;
        }

       protected:
        bool handleRequest(Request &req) override {
          if (AppObject::handleRequest(req))
            return true;
          if (req.is("check")) {
            if (_manualCheck) {
              req.respondError(ErrBusy);
              return true;
            }
            _manualCheck.reset(req.makeResponse());
            return true;
          }
          return false;
        }
        JsonDocument *getConfig(RequestContext &ctx) override {
          auto size = JSON_OBJECT_SIZE(3);
          auto doc = new JsonDocument(); /* size */
          auto root = doc->to<JsonObject>();
          json::to(root, "autoupdate", _autoUpdate);
          json::to(root, "autocheck", _autoCheck);
          json::to(root, "checkinterval", _checkInterval);
          return doc;
        }

        bool setConfig(RequestContext &ctx) override {
          bool changed = false;
          json::from(cfg["autoupdate"], _autoUpdate, &changed);
          json::from(cfg["autocheck"], _autoCheck, &changed);
          json::from(cfg["checkinterval"], _checkInterval, &changed);
          return changed;
        }

        size_t stateSize() {
          return 0;
        }
        JsonDocument *getState(const JsonVariantConst args) override {
          auto size = 0;
          if (_running)
            size += JSON_OBJECT_SIZE(1);
          if (_lastCheck)
            size += JSON_OBJECT_SIZE(1);
          if (newVersion)
            size += JSON_OBJECT_SIZE(1) +
                    JSON_STRING_SIZE(newVersion->version.toString().size());
          auto errsize = _errors.jsonSize();
          if (errsize)
            size += JSON_OBJECT_SIZE(1) + errsize;
          auto doc = new JsonDocument(); /* size */
          auto root = doc->to<JsonObject>();
          if (_running)
            json::to(root, "running", _running);
          if (newVersion)
            json::to(root, "newver", newVersion->version.toString());
          if (_lastCheck)
            json::to(root, "lastcheck", _lastCheck);
          if (errsize)
            _errors.toJson(root);
          return doc;
        }

       private:
        std::string _vendorUrl = (CONFIG_ESP32M_NET_OTA_VENDOR_URL);
        bool _running = false;
        bool _autoUpdate = false;
        bool _autoCheck = true;
        int _checkInterval = CONFIG_ESP32M_NET_OTA_CHECK_INTERVAL;
        unsigned long _lastCheck = 0, _lastUpdateAttempt = 0;
        std::unique_ptr<Response> _manualCheck;
        ErrorList _errors;
        Check() {}
        bool shouldCheckForUpdates() {
          if (_running)
            return false;
          if (_manualCheck)
            return true;
          if (!isDnsAvailable())
            return false;
          if (_autoCheck) {
            auto now = millis();
            if ((!_lastCheck && now > 30 * 1000) ||
                (now - _lastCheck > 1000 * 60 * _checkInterval))
              return true;
          }
          return false;
        }
        esp_err_t checkForUpdates() {
          _lastCheck = millis();
          newVersion.reset();
          _errors.clear();
          std::string baseUrl(CONFIG_ESP32M_NET_OTA_VENDOR_URL);
          if (baseUrl.empty())
            return error(ErrNoUrl);

          std::string mfn(App::instance().name());
          mfn += ".json";
          Url mfu(mfn.c_str(), baseUrl.c_str());
          std::string url(mfu.toString());
          /*logW("trying to fetch %s %s %s", mfn.c_str(), baseUrl.c_str(),
               url.c_str());*/
          UrlResourceRequest req(url.c_str());
          auto res = http::Client::instance().obtain(req);
          if (!res || !res->size()) {
            logW("failed to fetch %s", req.url());
            _errors.append(req.errors());
            return error(req.errors());
          }
          auto doc = json::parse((const char *)res->data(), res->size());
          delete res;
          if (doc) {
            Version current(App::instance().version());
            Version best(current);
            auto root = doc->as<JsonObjectConst>();
            auto firmware = root["firmware"].as<JsonObjectConst>();
            if (firmware) {
              string bestBinary;
              for (const auto &i : firmware) {
                Version ver(i.key().c_str());
                if (ver > best) {
                  best = ver;
                  bestBinary = i.value().as<std::string>();
                }
              }
              if (best != current) {
                Url mfu(bestBinary.c_str(), baseUrl.c_str());
                url = mfu.toString();
                logI("detected new version %s %s", best.toString().c_str(),
                     url.c_str());
                req = UrlResourceRequest(url.c_str());
                auto info = http::Client::instance().describe(req);
                if (info) {
                  newVersion.reset(
                      new ota::VersionInfo{.version = best, .url = url});
                } else {
                  _errors.append(req.errors());
                  return error(_errors);
                }
              }
            }
          }
          delete doc;
          if (_manualCheck) {
            if (newVersion) {
              auto nv = newVersion->version.toString();
              doc = new JsonDocument(); /* JSON_STRING_SIZE(nv.size()) */
              doc->set(nv);
              Response::respond(_manualCheck, doc);
            } else
              Response::respond(_manualCheck);
          }
          return ESP_OK;
        }
        esp_err_t error(esp_err_t err) {
          _errors.check(err);
          error(_errors);
          return err;
        }
        esp_err_t error(const char *code) {
          _errors.add(code);
          return error(_errors);
        }
        esp_err_t error(ErrorList &list) {
          if (_manualCheck)
            Response::respondError(_manualCheck, list);
          return ESP_FAIL;
        }
      };
#endif

    }  // namespace ota

    const char *Ota::KeyOtaBegin = "begin";
    const char *Ota::KeyOtaEnd = "end";

    Ota::Ota() {
      _mutex = &locks::get(ota::Name);
#if CONFIG_ESP32M_NET_OTA_CHECK_FOR_UPDATES
      ota::Check::instance();
#endif
      xTaskCreate([](void *self) { ((Ota *)self)->run(); }, "m/ota", 4096, this,
                  tskIDLE_PRIORITY + 1,
                  &_task);  // 2kb stack is not enough
    }

    const char *Ota::name() const {
      return ota::Name;
    };

    ota::Features Ota::features() const {
      ota::Features f{};
#if CONFIG_ESP32M_NET_OTA_CHECK_FOR_UPDATES
      ota::Check::instance().getFeatures(f);
#endif
      return f;
    }

    ota::StateFlags Ota::flags() {
      ota::StateFlags f{};
#if CONFIG_ESP32M_NET_OTA_CHECK_FOR_UPDATES
      ota::Check::instance().getStateFlags(f);
#endif
      f.updating = _updating;
      return f;
    }

    JsonDocument *Ota::getState(RequestContext &ctx) {
      // auto size = JSON_OBJECT_SIZE(1 /*flags*/ + (_updating ? 2 : 0));
      auto doc = new JsonDocument(); /* size */
      auto root = doc->to<JsonObject>();
      json::to(root, "flags", flags().value);
      if (_updating) {
        root["progress"] = _progress;
        root["total"] = _total;
      }
      return doc;
    }

    JsonDocument *Ota::getConfig(RequestContext &ctx) {
      auto dl = _savedUrl.size();
      /*auto size = JSON_OBJECT_SIZE(1);
      if (dl)
        size += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(dl);*/
      auto doc = new JsonDocument(); /* size */
      auto root = doc->to<JsonObject>();
      json::to(root, "features", features().value);
      if (dl)
        json::to(root, "url", _savedUrl);
      return doc;
    }

    bool Ota::setConfig(RequestContext &ctx) {
      bool changed = false;
      auto cfg = ctx.data.as<JsonObjectConst>();
      json::from(cfg["url"], _savedUrl, &changed);
      return changed;
    }

    bool Ota::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("update")) {
        auto sf = flags();
        if (sf.checking || sf.updating || !_pendingUrl.empty()) {
          req.respondError(ErrBusy);
          return true;
        }
        auto f = features();
        if (!f.vendorOnly) {
          auto data = req.data();
          if (data["save"]) {
            RequestContext ctx(req, req.data());
            if (setConfig(ctx))
              config::Changed::publish(this);
          }
          json::from(data["url"], _pendingUrl);
        }
#if CONFIG_ESP32M_NET_OTA_CHECK_FOR_UPDATES
        auto nv = ota::Check::instance().newVersion.get();
        if (_pendingUrl.empty() && nv)
          _pendingUrl = nv->url;
#endif
        if (_pendingUrl.empty())
          req.respondError(ota::ErrNoUrl);
        else {
          xTaskNotifyGive(_task);
          req.respond();
        }
      } else
        return false;
      return true;
    }

    esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client) {
      Ota::instance()._httpClient = http_client;
      return ESP_OK;
    }

    void Ota::run() {
      esp_task_wdt_add(NULL);
      for (;;) {
#if CONFIG_ESP32M_NET_OTA_CHECK_FOR_UPDATES
        ota::Check::instance().run(_pendingUrl);
#endif
        if (!_pendingUrl.empty())
          perform(_pendingUrl.c_str());

        esp_task_wdt_reset();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(_updating ? 1 : 1000));
      }
    }

    void Ota::perform(const char *url) {
      begin();
      logI("http updater starting for %s", url);
      esp_http_client_config_t config = {};
      config.url = url;
      config.timeout_ms = 60 * 1000;
      config.skip_cert_common_name_check = true;
      esp_https_ota_config_t ota_config = {};
      ota_config.http_config = &config;
      ota_config.http_client_init_cb = _http_client_init_cb;
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
          delay(1);
          esp_task_wdt_reset();
        }
        if (err == ESP_OK)
          err = esp_https_ota_finish(https_ota_handle);
        else
          esp_https_ota_abort(https_ota_handle);
      }
      end();
      if (err == ESP_OK) {
        logI("OTA update was successful, rebooting...");
        delay(1000);
        esp_task_wdt_reset();
        App::restart();
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
      _updating = true;
      ota::_isRunning = true;
      Broadcast::publish(name(), KeyOtaBegin);
      _mutex->lock();
    }

    void Ota::end() {
      Broadcast::publish(name(), KeyOtaEnd);
      _pendingUrl.clear();
      _httpClient = nullptr;
      _progress = 0;
      _total = 0;
      _mutex->unlock();
      _updating = false;
      ota::_isRunning = false;
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