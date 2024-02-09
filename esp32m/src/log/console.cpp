#include "esp32m/log/console.hpp"
#include "esp32m/net/ota.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <esp_rom_uart.h>
#include <string.h>

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#  define ESP_ROM_PUTC esp_rom_output_putc
#else
#  define ESP_ROM_PUTC esp_rom_uart_putc
#endif
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
        std::lock_guard guard(_mutex);
        auto l = strlen(message);
        for (auto i = 0; i < l; i++) ESP_ROM_PUTC(message[i]);
        ESP_ROM_PUTC('\n');
      }
      return true;
    }
  }  // namespace log

}  // namespace esp32m