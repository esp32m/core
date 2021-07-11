#ifdef ARDUINO
#  include <Arduino.h>
#else
#  include "nvs_flash.h"
#endif

#include <esp_image_format.h>
#include <esp_int_wdt.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <sdkconfig.h>

#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/config/changed.hpp"
#include "esp32m/io/spiffs.hpp"
#include "esp32m/json.hpp"

namespace esp32m {

  const char *EventInit::NAME = "init";
  App *_appInstance = nullptr;

  const char *Stateful::KeyStateSet = "state-set";
  const char *Stateful::KeyStateGet = "state-get";

  bool Stateful::handleStateRequest(Request &req) {
    const char *name = req.name();
    if (!strcmp(name, KeyStateGet)) {
      DynamicJsonDocument *state = getState(req.data());
      if (state) {
        json::check(this, state, "getState()");
        req.respond(stateName(), *state, false);
        delete state;
      }
      return true;
    } else if (!strcmp(name, KeyStateSet)) {
      DynamicJsonDocument *result = nullptr;
      JsonVariantConst data =
          req.isBroadcast() ? req.data()[stateName()] : req.data();
      if (data.isUndefined())
        return true;
      setState(data, &result);
      if (result) {
        json::check(this, result, "setState()");
        req.respond(stateName(), *result);
        delete result;
      } else
        req.respond(stateName(), json::null<JsonVariantConst>(), false);
      return true;
    }
    return false;
  }

  Interactive::Interactive() {
    EventManager::instance().subscribe([this](Event &ev) {
      Request *req;
      if (Request::is(ev, interactiveName(), &req))
        handleRequest(*req);
      else
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
      auto desc = esp_ota_get_app_description();
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

  void App::Init::setConfigStore(ConfigStore *store) {
    _appInstance->_config.reset(new Config(store));
  }

  App ::App(const char *name, const char *version)
      : _name(name), _version(version) {
#ifdef ARDUINO
#  if !CONFIG_AUTOSTART_ARDUINO
    initArduino();
#  endif
    Serial.begin(115200);
#else
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
#endif
    esp_int_wdt_init();
    esp_task_wdt_init(_wdtTimeout, true);
    logI("starting %s", _version ? _version : "");
    _sketchSize = sketchSize();
  }

  bool App::initialized() {
    return _appInstance &&
           _appInstance->_curInitLevel > _appInstance->_maxInitLevel;
  }

  App &App::instance() {
    if (!_appInstance)
      abort();
    return *_appInstance;
  }

  void App::init() {
    if (initialized())
      return;
    if (!_config)
      _config.reset(new Config(spiffs::newConfigStore()));
    _config->load();
    for (int i = 0; i <= _maxInitLevel; i++) {
      EventInit evt(i);
      logI("init level %i", i);
      evt.publish();
      _curInitLevel++;
    }
    logI("initialization complete");
    xTaskCreate([](void *self) { ((App *)self)->run(); }, "m/app", 5120, this,
                tskIDLE_PRIORITY, &_task);
  }

  bool App::handleEvent(Event &ev) {
    if (EventConfigChanged::is(ev)) {
      if (((EventConfigChanged *)&ev)->saveNow()) {
        _config->save();
        _configDirty = 0;
      } else
        _configDirty = millis();
      return true;
    }
    return false;
  }

  bool App::handleRequest(Request &req) {
    if (AppObject::handleRequest(req))
      return true;
    if (req.is("restart")) {
      req.respond();
      esp_restart();
      return true;
    }
    if (req.is("reset")) {
      config()->reset();
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
    info["uptime"] = esp_timer_get_time() / 1000;
    if (_version)
      info["version"] = _version;
    info["built"] = __DATE__ " " __TIME__;
    info["sdk"] = esp_get_idf_version();
    info["size"] = _sketchSize;
    return doc;
  }
}  // namespace esp32m
