#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32m/debug/pcf857x.hpp"
#include "esp32m/json.hpp"

namespace esp32m {
  namespace debug {

    DynamicJsonDocument *Pcf857x::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(2));
      auto root = doc->to<JsonObject>();
      uint16_t port;
      ESP_ERROR_CHECK_WITHOUT_ABORT(_dev->read(port));
      root["port"] = port;
      return doc;
    }

    void Pcf857x::setState(const JsonVariantConst state,
                           DynamicJsonDocument **result) {
      esp32m::json::checkSetResult(
          ESP_ERROR_CHECK_WITHOUT_ABORT(_dev->write(state["port"])), result);
    }

  }  // namespace debug
}  // namespace esp32m