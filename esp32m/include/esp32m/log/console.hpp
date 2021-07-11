#pragma once

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace log {
    /**
     * Sends output to default UART (usually UART0) using
     * ets_write_char_uart(...)
     */
    class Console : public FormattingAppender {
     public:
      Console(const Console &) = delete;
      static Console &instance();

     private:
      Console() {}

     protected:
      virtual bool append(const char *message);
    };
  }  // namespace log
}  // namespace esp32m