#ifdef ARDUINO

#  include <string.h>

#  include "esp32m/log/uart.hpp"

#  include <esp32-hal.h>
#  include <soc/uart_reg.h>

namespace esp32m {

  namespace log {
    Uart::Uart(uint8_t num) {
      switch (num) {
        case 1:
          _portBase = DR_REG_UART1_BASE;
          break;
        case 2:
          _portBase = DR_REG_UART2_BASE;
          break;
        default:
          _portBase = DR_REG_UART_BASE;
          break;
      }
    }

    void uart_write_char(uint32_t portBase, char c) {
      while (((ESP_REG(0x01C + portBase) >> UART_TXFIFO_CNT_S) & 0x7F) == 0x7F)
        ;
      ESP_REG(portBase) = c;
    }

    bool Uart::append(const char *message) {
      if (message) {
        auto l = strlen(message);
        for (auto i = 0; i < l; i++) uart_write_char(_portBase, message[i]);
        uart_write_char(_portBase, '\r');
        uart_write_char(_portBase, '\n');
      }

      return true;
    }
  }  // namespace log

}  // namespace esp32m

#endif