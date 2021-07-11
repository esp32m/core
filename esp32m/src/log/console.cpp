#include <string.h>

#include "esp32m/log/console.hpp"

namespace esp32m {

  namespace log {
    Console &Console::instance() {
      static Console i;
      return i;
    }

    bool Console::append(const char *message) {
      if (message) {
        auto l = strlen(message);
        for (auto i = 0; i < l; i++) ets_write_char_uart(message[i]);
        ets_write_char_uart('\n');
      }

      return true;
    }
  }  // namespace log

}  // namespace esp32m