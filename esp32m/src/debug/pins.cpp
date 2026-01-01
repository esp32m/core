#include "esp32m/debug/pins.hpp"

namespace esp32m {

  namespace debug {

    Pins* usePins() {
      return &Pins::instance();
    }
  }  // namespace debug
}  // namespace esp32m