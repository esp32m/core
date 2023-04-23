#include <esp_task_wdt.h>

#include <ArduinoJson.h>

#include "esp32m/app.hpp"
#include "esp32m/events.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/events/response.hpp"

#include "esp32m/bus/i2c.hpp"
#include "esp32m/bus/scanner/i2c.hpp"

namespace esp32m {
  namespace bus {
    namespace scanner {

      StaticJsonDocument<JSON_ARRAY_SIZE(1)> _i2cScannerErrors;

      I2C::I2C() {
        _i2cScannerErrors.add("busy");
        _i2cScannerErrors.add("init");
      }

      DynamicJsonDocument *I2C::getState(const JsonVariantConst args) {
        DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(5));
        auto root = doc->to<JsonObject>();
        root["sda"] = _pinSDA;
        root["scl"] = _pinSCL;
        root["freq"] = _freq;
        root["from"] = _startId;
        root["to"] = _endId;
        return doc;
      }

      bool I2C::handleRequest(Request &req) {
        if (AppObject::handleRequest(req))
          return true;
        if (req.is("i2c", "scan")) {
          auto data = req.data();
          if (_pendingResponse) {
            req.respond("i2c", _i2cScannerErrors[0], true);
          } else {
            _pinSDA = data["sda"] | I2C_MASTER_SDA;
            _pinSCL = data["scl"] | I2C_MASTER_SCL;
            _freq = data["freq"] | 100000;
            _startId = data["from"] | 3;
            _endId = data["to"] | 120;
            if (_startId < 1)
              _startId = 1;
            else if (_startId > 127)
              _startId = 127;
            if (_endId < _startId)
              _endId = _startId;
            else if (_endId > 127)
              _endId = 127;
            _pendingResponse = req.makeResponse();
            xTaskNotifyGive(_task);
          }
          return true;
        }
        return false;
      }

      void I2C::handleEvent(Event &ev) {
        if (EventInit::is(ev, 0)) {
          xTaskCreate([](void *self) { ((I2C *)self)->run(); }, "m/s/i2c", 4096,
                      this, 1, &_task);
        }
      }

      I2C &I2C::instance() {
        static I2C i;
        return i;
      }

      void I2C::run() {
        esp_task_wdt_add(NULL);
        for (;;) {
          esp_task_wdt_reset();
          if (_pendingResponse) {
            memset(&_ids, 0, sizeof(_ids));
            esp32m::I2C i2c(0, I2C_NUM_0, (gpio_num_t)_pinSDA,
                            (gpio_num_t)_pinSCL, _freq);
            for (int i = _startId; i <= _endId; i++) {
              i2c.setAddr(i);
              esp_err_t err = i2c.write(nullptr, 0, nullptr, 0);
              if (err == ESP_OK) {
                logI("found i2c device at 0x%02x", i);
                _ids[i >> 3] |= 1 << (i & 7);
              }
              esp_task_wdt_reset();
            }
            DynamicJsonDocument *doc = new DynamicJsonDocument(
                JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(sizeof(_ids)));
            auto ids = doc->createNestedArray("ids");
            for (auto i = 0; i < sizeof(_ids); i++) ids.add(_ids[i]);
            _pendingResponse->setData(doc);
            _pendingResponse->publish();
            delete _pendingResponse;
            _pendingResponse = nullptr;
          }
          ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        }
      }
      I2C *useI2C() {
        return &I2C::instance();
      }
    }  // namespace scanner
  }    // namespace bus
}  // namespace esp32m