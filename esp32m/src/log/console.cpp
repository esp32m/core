#include "esp32m/log/console.hpp"
#include "esp32m/net/ota.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <esp_rom_uart.h>
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
        for (auto i = 0; i < l; i++) esp_rom_uart_putc(message[i]);
        esp_rom_uart_putc('\n');
      }

      return true;
    }
  }  // namespace log

}  // namespace esp32m