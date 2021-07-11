#ifdef ARDUINO

#  pragma once
#  include "esp32m/logging.hpp"

namespace esp32m {
  namespace log {
    /**
     * Sends output to the specified UART
     */
    class Uart : public FormattingAppender {
     public:
      Uart(const Uart &) = delete;
      Uart(uint8_t num = 0);

     private:
      uint32_t _portBase;

     protected:
      virtual bool append(const char *message);
    };
  }  // namespace log
}  // namespace esp32m

#endif