#include "esp32m/log/console.hpp"
#include "esp32m/net/ota.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <esp32/rom/ets_sys.h>
#include <string.h>

namespace esp32m {

  namespace log {
    Console &Console::instance() {
      static Console i;
      return i;
    }

    bool Console::append(const char *message) {
      if (!xPortCanYield())  // called from ISR
        return false;
      if (net::ota::isRunning())
        return false;
      if (message) {
        auto l = strlen(message);
        for (auto i = 0; i < l; i++) ets_write_char_uart(message[i]);
        ets_write_char_uart('\n');
      }

      return true;
    }
  }  // namespace log

}  // namespace esp32m