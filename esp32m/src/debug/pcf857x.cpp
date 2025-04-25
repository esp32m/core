#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32m/debug/pcf857x.hpp"
#include "esp32m/json.hpp"

namespace esp32m {
  namespace debug {

    JsonDocument *Pcf857x::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_OBJECT_SIZE(2) */
      auto root = doc->to<JsonObject>();
      uint16_t port;
      ESP_ERROR_CHECK_WITHOUT_ABORT(_dev->read(port));
      root["port"] = port;
      return doc;
    }

    void Pcf857x::setState(RequestContext &ctx) {
      ctx.errors.check(
          ESP_ERROR_CHECK_WITHOUT_ABORT(_dev->write(ctx.data["port"])));
    }

  }  // namespace debug
}  // namespace esp32m