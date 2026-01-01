#include "esp32m/net/term/mqtt.hpp"

#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/net/mqtt.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      void MqttConsumer::consume(std::string line) {
        auto& mqtt = net::Mqtt::instance();
        if (!mqtt.isReady() || line.empty())
          return;

        auto topic =
            string_printf("esp32m/%s/uart", App::instance().hostname());
        mqtt.enqueue(topic.c_str(), line.c_str());
      }

    }  // namespace term
  }  // namespace net
}  // namespace esp32m
