#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include "esp32m/net/mqtt.hpp"
#include "esp32m/app.hpp"
#include "esp32m/log/mqtt.hpp"
#include "esp32m/net/ota.hpp"
namespace esp32m {

  namespace log {
    Mqtt &Mqtt::instance() {
      static Mqtt i;
      return i;
    }

    bool Mqtt::append(const char *message) {
      if (!xPortCanYield()) // called from ISR
        return false;
      if (net::ota::isRunning())
        return false;
      auto &mqtt = net::Mqtt::instance();
      if (!mqtt.isReady())
        return false;
      if (!message)
        return true;
      if (!_topic && App::instance().hostname()) {
        if (asprintf(&_topic, "esp32m/log/%s", App::instance().hostname()) < 0)
          _topic = nullptr;
      }
      if (!_topic)
        return false;
      //    return true;
      return mqtt.publish(_topic, message);
    }
  }  // namespace log
}  // namespace esp32m