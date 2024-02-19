#include "nvs_flash.h"

#include <esp_image_format.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_private/esp_int_wdt.h>
#include <esp_task_wdt.h>
#include <hal/efuse_hal.h>
#include <lwip/apps/netbiosns.h>
#include <sdkconfig.h>

#include <dirent.h>

#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/config/vfs.hpp"
#include "esp32m/debug/button.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/fs/littlefs.hpp"
#include "esp32m/fs/spiffs.hpp"
#include "esp32m/json.hpp"
#include "esp32m/log/console.hpp"
#include "esp32m/log/udp.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {

  App *_appInstance = nullptr;

  const char *AppObject::KeyStateSet = "state-set";
  const char *AppObject::KeyStateGet = "state-get";

  void EventDone::publish(DoneReason reason) {
    EventDone ev(reason);
    EventManager::instance().publishBackwards(ev);
  }

  AppObject::AppObject() {
    EventManager::instance().subscribe([this](Event &ev) {
      Request *req;
      EventDescribe *ed;
      if (Request::is(ev, interactiveName(), &req))
        handleRequest(*req);
      else if (EventDescribe::is(ev, &ed)) {
        ed->add(name(), descriptor());
      } else
        handleEvent(ev);
    });
  };

  bool AppObject::handleRequest(Request &req) {
    if (handleConfigRequest(req))
      return true;
    if (handleStateRequest(req))
      return true;
    return false;
  };

  bool AppObject::handleStateRequest(Request &req) {
    const char *name = req.name();
    const char *iname = interactiveName();
    if (!strcmp(name, KeyStateGet)) {
      DynamicJsonDocument *state = getState(req.data());
      if (state) {
        json::check(this, state, "getState()");
        req.respond(iname, *state, false);
        delete state;
      }
      return true;
    } else if (!strcmp(name, KeyStateSet)) {
      DynamicJsonDocument *result = nullptr;
      JsonVariantConst data =
          req.isBroadcast() ? req.data()[iname] : req.data();
      if (data.isUnbound())
        return true;
      setState(data, &result);
      if (result) {
        json::check(this, result, "setState()");
        req.respond(iname, *result);
        delete result;
      } else
        req.respond(iname, json::null<JsonVariantConst>(), false);
      return true;
    }
    return false;
  }

  bool AppObject::handleConfigRequest(Request &req) {
    const char *name = req.name();
    const char *cname = interactiveName();
    JsonVariantConst reqData = req.data();
    bool internalRequest = !req.origin();
    if (!strcmp(name, Config::KeyConfigGet)) {
      // internal requests means request to save config
      // we don't want to save default config (the one that never changed)
      if (internalRequest && !_configured)
        return true;
      RequestContext ctx(req, reqData);
      DynamicJsonDocument *config = getConfig(ctx);
      if (config) {
        json::check(this, config, "getConfig()");
        req.respond(cname, *config, false);
        delete config;
      }
      return true;
    } else if (!strcmp(name, Config::KeyConfigSet)) {
      JsonVariantConst data = req.isBroadcast() ? reqData[cname] : reqData;
      if (data.isUnbound())
        return true;
      RequestContext ctx(req, data);
      DynamicJsonDocument *result = nullptr;
      if (setConfig(data, &result)) {
        config::Changed::publish(this);
        Broadcast::publish(cname, "config-changed");
      }
      if (result) {
        json::check(this, result, "setConfig()");
        req.respond(cname, *result);
        delete result;
      } else
        req.respond(cname, json::null<JsonVariantConst>(), false);
      return true;
    }
    return false;
  }

  static uint32_t sketchSize() {
    esp_image_metadata_t data;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
      return 0;
    const esp_partition_pos_t running_pos = {
        .offset = running->address,
        .size = running->size,
    };
    data.start_addr = running_pos.offset;
    esp_image_verify(ESP_IMAGE_VERIFY, &running_pos, &data);
    return data.image_len;
  }

  App::Init::Init(const char *name, const char *version) {
    if (_appInstance) {
      _appInstance->logger().log(log::Level::Warning,
                                 "app already initialized");
      return;
    }
    if (!name || !version) {
      auto desc = esp_app_get_description();
      if (!name)
        name = desc->project_name;
      if (!version)
        version = desc->version;
    }

    _appInstance = new App(name, version);
  }

  App::Init::~Init() {
    _appInstance->init();
  }

  void App::Init::requestInitLevels(uint8_t maxInitLevel) {
    _appInstance->_maxInitLevel = maxInitLevel;
  }

  void App::Init::setConfigStore(config::Store *store) {
    _appInstance->_config.reset(new Config(store));
  }

  void App::Init::inferHostname(int limit, int macDigits) {
    uint8_t mac[6];
    char digit[3];
    efuse_hal_get_mac(mac);
    if (macDigits > 6)
      macDigits = 6;
    if (macDigits < 1)
      macDigits = 1;
    if (limit < macDigits * 2)
      limit = macDigits * 2;
    std::string hostname = _appInstance->_name.substr(0, limit - macDigits * 2);
    for (int i = macDigits - 1; i >= 0; i--) {
      sprintf(digit, "%02hhx", mac[i]);
      hostname += digit;
    }
    _appInstance->_hostname = hostname;
    _appInstance->_defaultHostname = hostname;
  }

  App::App(const char *name, const char *version)
      : _version(version), _props("app") {
    _name = name;
    _hostname = name;
    _defaultHostname = name;
    uint8_t macOrEui[8];
    char buf[13];
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_efuse_mac_get_default(macOrEui)) ==
        ESP_OK) {
      sprintf(buf, "%02x%02x%02x%02x%02x%02x", macOrEui[0], macOrEui[1],
              macOrEui[2], macOrEui[3], macOrEui[4], macOrEui[5]);
      _props.set("uid", buf);
    }
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
    esp_int_wdt_init();
    esp_task_wdt_config_t wdtc = {
        .timeout_ms = _wdtTimeout * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true};
    esp_task_wdt_deinit();
    esp_task_wdt_init(&wdtc);
    _sketchSize = sketchSize();
#if CONFIG_ESP32M_LOG_CONSOLE
    log::addAppender(&log::Console::instance());
#endif
#if CONFIG_ESP32M_LOG_UDP
    _udpLogger = new log::Udp(CONFIG_ESP32M_LOG_UDP_HOST);
    log::addBufferedAppender(_udpLogger);
#endif
#if CONFIG_ESP32M_LOG_QUEUE
#  if CONFIG_ESP32M_LOG_QUEUE_SIZE
    log::useQueue(CONFIG_ESP32M_LOG_QUEUE_SIZE);
#  else
    log::useQueue();
#  endif
#endif
#if CONFIG_ESP32M_LOG_HOOK_ESPIDF
    log::hookEsp32Logger();
#endif
#if CONFIG_ESP32M_LOG_HOOK_UART
    log::hookUartLogger();
#endif
  }

  bool App::initialized() {
    return _appInstance &&
           _appInstance->_curInitLevel > _appInstance->_maxInitLevel;
  }

  void App::restart() {
    delay(100);  // this won't hurt, especually for buffered logs to catch up
    EventDone::publish(DoneReason::Restart);
    esp_restart();
  }

  App &App::instance() {
    if (!_appInstance)
      abort();
    return *_appInstance;
  }

  void App::init() {
    if (initialized())
      return;
    logI("starting %s %s", _hostname.c_str(), _version ? _version : "");

    DIR *dir = opendir("/");
    if (dir) {
      /* root already mounted */
      logI("root found");
      closedir(dir);
    } else {
#if CONFIG_ESP32M_FS_ROOT_LITTLEFS
      fs::Littlefs::instance();
#else
      fs::Spiffs::instance();
#endif
    }
    if (!_config)
      _config.reset(new Config(new config::Vfs("/config.json")));
    _config->load();
    for (int i = 0; i <= _maxInitLevel; i++) {
      EventInit evt(i);
      logI("init level %i", i);
      evt.publish();
      _curInitLevel++;
    }
    xTaskCreate([](void *self) { ((App *)self)->run(); }, "m/app", 5120, this,
                tskIDLE_PRIORITY, &_task);
    EventInited inited;
    inited.publish();
    logI("initialization complete");
  }

  void App::handleEvent(Event &ev) {
    if (config::Changed::is(ev)) {
      if (((config::Changed *)&ev)->saveNow()) {
        _config->save();
        _configDirty = 0;
      } else
        _configDirty = millis();
    } else if (debug::button::Command::is(ev, 1)) {
      auto doc = _config->read();
      if (doc) {
        json::dump(this, doc->as<JsonVariantConst>(), "saved config");
        delete doc;
      }
    }
  }

  void App::resetHostname() {
    setHostname(_defaultHostname.c_str());
  }

  bool App::handleRequest(Request &req) {
    if (AppObject::handleRequest(req))
      return true;
    if (req.is("restart")) {
      req.respond();
      restart();
      return true;
    } else if (req.is("reset")) {
      config()->reset();
      req.respond();
      return true;
    } else if (req.is("reset-hostname")) {
      resetHostname();
      _config->save();
      req.respond();
      return true;
    } else if (req.is("describe")) {
      EventDescribe ev;
      EventManager::instance().publish(ev);
      size_t size = JSON_OBJECT_SIZE(ev.descriptors.size());
      if (size) {
        for (auto &e : ev.descriptors) {
          size += JSON_STRING_SIZE(e.first.size()) + json::measure(e.second);
        }
        auto doc = new DynamicJsonDocument(size);
        auto root = doc->to<JsonObject>();
        for (auto &e : ev.descriptors) {
          root[e.first] = e.second;
        }
        req.respond(root, false);
        delete doc;
      } else
        req.respond();
      return true;
    }
    return false;
  }

  void App::run() {
    esp_task_wdt_add(NULL);
    for (;;) {
      esp_task_wdt_reset();
      if (_configDirty && (millis() - _configDirty > 1000)) {
        _configDirty = 0;
        _config->save();
      }
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
  }

  DynamicJsonDocument *App::getState(const JsonVariantConst args) {
    size_t size = JSON_OBJECT_SIZE(
        1 + 8);  // root: name, time, uptime, version, built, sdk, size, space
    auto doc = new DynamicJsonDocument(size);
    JsonObject info = doc->to<JsonObject>();

    info["name"] = _name;
    time_t t;
    time(&t);
    info["time"] = t;
    info["uptime"] = millis();
    if (_version)
      info["version"] = _version;
    info["built"] = __DATE__ " " __TIME__;
    info["sdk"] = esp_get_idf_version();
    info["size"] = _sketchSize;
    return doc;
  }

  DynamicJsonDocument *App::getConfig(RequestContext &ctx) {
    size_t size = JSON_OBJECT_SIZE(4)  // hostname, udplog: enabled, host
                  + JSON_STRING_SIZE(_hostname.size());
    if (_udpLogger)
      size += JSON_STRING_SIZE(_udpLogger->getHost().size());

    auto doc = new DynamicJsonDocument(size);
    auto root = doc->to<JsonObject>();
    json::to(root, "hostname", _hostname);
    auto udplog = root.createNestedObject("udplog");
    if (_udpLogger) {
      json::to(udplog, "enabled", _udpLogger->isEnabled());
      json::to(udplog, "host", _udpLogger->getHost());
    } else {
      json::to(udplog, "enabled", false);
    }
    return doc;
  }

  bool App::setConfig(const JsonVariantConst data,
                      DynamicJsonDocument **result) {
    JsonObjectConst root = data.as<JsonObjectConst>();
    std::string next = _hostname;
    bool changed = false;
    json::from(root["hostname"], next, &changed);
    if (changed)
      setHostname(next.c_str());
    auto udplog = root["udplog"];
    if (udplog) {
      auto enabled = udplog["enabled"].as<bool>();
      if (enabled) {
        if (!_udpLogger) {
          _udpLogger = new log::Udp();
          log::addBufferedAppender(_udpLogger);
        }
      }
      if (_udpLogger) {
        if (_udpLogger->isEnabled() != enabled) {
          _udpLogger->enable(enabled);
          changed = true;
        }
        auto vhost = udplog["host"];
        if (vhost) {
          std::string oldHost = _udpLogger->getHost();
          std::string newHost = vhost;
          if (oldHost != newHost) {
            _udpLogger->setHost(newHost.c_str());
            changed = true;
          }
        }
      }
    }
    return changed;
  }

  void App::setHostname(const char *hostname) {
    std::string next(hostname ? hostname : _name.c_str());
    std::string prev = _hostname;
    if (next != prev) {
      _hostname = next.substr(0, 31);
      logI("my hostname is %s", _hostname.c_str());
      EventPropChanged::publish("app", "hostname", prev, _hostname);
      auto nbname = _hostname.substr(0, 15).c_str();
      netbiosns_set_name(nbname);
    }
  }

}  // namespace esp32m
