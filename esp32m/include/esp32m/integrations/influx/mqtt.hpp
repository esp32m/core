#include "esp32m/device.hpp"

namespace esp32m {
  namespace integrations {
    namespace influx {
      class Mqtt : public dev::StateEmitter {
       public:
        const char *name() const override {
          return "influx-mqtt";
        };

        static Mqtt &instance() {
          static Mqtt i;
          return i;
        }

       protected:
        void handleEvent(Event &ev) override;
        void emit(std::vector<const dev::Component *> sensors) override;

       private:
        Mqtt(){};
        char *_sensorsTopic = nullptr;
      };

      static inline Mqtt *useMqtt() {
        return &Mqtt::instance();
      }

    }  // namespace influx
  }    // namespace integrations
}  // namespace esp32m