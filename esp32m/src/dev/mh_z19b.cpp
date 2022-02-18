#include "esp32m/dev/mh_z19b.hpp"

namespace esp32m {

  namespace dev {

    MhZ19b::MhZ19b(uart_port_t uart_num, const char *name)
        : Device(Flags::HasSensors), _name(name) {}

    bool MhZ19b::pollSensors() {
      return true;
    }
    bool MhZ19b::initSensors() {
      return true;
    }

    DynamicJsonDocument *MhZ19b::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_ARRAY_SIZE(2));
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_value);
      return doc;
    }

  }  // namespace dev
}  // namespace esp32m