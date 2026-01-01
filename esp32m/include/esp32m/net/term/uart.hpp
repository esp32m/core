#pragma once

#include <atomic>
#include <cstdint>

#include <driver/uart.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      class IRxTap {
       public:
        virtual ~IRxTap() = default;
        virtual void onRx(const uint8_t *data, size_t len) = 0;
      };

      struct UartConfig {
        uart_port_t port = UART_NUM_0;
        int txGpio = UART_PIN_NO_CHANGE;
        int rxGpio = UART_PIN_NO_CHANGE;
        uint32_t baudrate = 115200;
        uart_word_length_t dataBits = UART_DATA_8_BITS;
        uart_parity_t parity = UART_PARITY_DISABLE;
        uart_stop_bits_t stopBits = UART_STOP_BITS_1;
        uart_hw_flowcontrol_t flowCtrl = UART_HW_FLOWCTRL_DISABLE;
        uint8_t rxFlowCtrlThresh = 0;
        uart_sclk_t sourceClk = UART_SCLK_DEFAULT;
        size_t rxBufSize = 4096;
        size_t txBufSize = 4096;
      };

      class Uart : public log::Loggable {
       public:
        Uart(const Uart &) = delete;
        Uart &operator=(const Uart &) = delete;

        static Uart &instance();

        const char *name() const override {
          return "term-uart";
        }

        const UartConfig &defaults() const {
          return _defaults;
        }

        uart_port_t port() const {
          return _defaults.port;
        }

        esp_err_t ensure();
        esp_err_t resetToDefaults();

        void setDefaults(const UartConfig &cfg);

        void setRxTap(IRxTap *tap) {
          _rxTap.store(tap);
        }

        esp_err_t apply(const UartConfig &cfg);

        esp_err_t setBaudrate(uint32_t baudrate);
        esp_err_t flushInput();
        esp_err_t waitTxDone(TickType_t timeout);

        int read(uint8_t *buf, size_t len, TickType_t timeout);
        int write(const uint8_t *data, size_t len);

       private:
        Uart();

        esp_err_t applyConfig(const UartConfig &cfg);

        UartConfig _defaults;

        std::atomic<IRxTap *> _rxTap = nullptr;
      };

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
