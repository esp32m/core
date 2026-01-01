#pragma once

#include "esp32m/net/term/monitor.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      class MqttConsumer : public IConsumer {
       public:
        void consume(std::string line) override;
        static MqttConsumer& instance() {
          static MqttConsumer i;
          return i;
        }

       private:
        MqttConsumer() {
          Monitor::instance().add(*this);
        }
      };
    }  // namespace term
  }  // namespace net
}  // namespace esp32m