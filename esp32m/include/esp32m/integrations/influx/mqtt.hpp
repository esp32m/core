#include "esp32m/device.hpp"

namespace esp32m {
  namespace integrations {
    namespace influx {
      class Mqtt : public sensor::StateEmitter {
       public:
        const char *name() const override {
          return "influx-mqtt";
        };

       protected:
        void handleEvent(Event &ev) override;
        void emit(std::vector<const Sensor *> sensors) override;

       private:
        char *_sensorsTopic = nullptr;
      };
    }  // namespace influx
  }    // namespace integrations
}  // namespace esp32m