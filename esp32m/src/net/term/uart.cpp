#include "esp32m/net/term/uart.hpp"

#include <esp_idf_version.h>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      Uart &Uart::instance() {
        static Uart i;
        return i;
      }

      Uart::Uart() {
        _defaults = UartConfig{};
      }

      void Uart::setDefaults(const UartConfig &cfg) {
        _defaults = cfg;
      }

      esp_err_t Uart::applyConfig(const UartConfig &cfg) {
        uart_config_t uartCfg{};
        uartCfg.baud_rate = static_cast<int>(cfg.baudrate);
        uartCfg.data_bits = cfg.dataBits;
        uartCfg.parity = cfg.parity;
        uartCfg.stop_bits = cfg.stopBits;
        uartCfg.flow_ctrl = cfg.flowCtrl;
        uartCfg.rx_flow_ctrl_thresh = cfg.rxFlowCtrlThresh;
        uartCfg.source_clk = cfg.sourceClk;

        esp_err_t err = uart_param_config(cfg.port, &uartCfg);
        if (err != ESP_OK) {
          logE("uart_param_config failed: %s", esp_err_to_name(err));
          return err;
        }

        err = uart_set_pin(cfg.port, cfg.txGpio, cfg.rxGpio, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
          logE("uart_set_pin failed: %s", esp_err_to_name(err));
          return err;
        }

        return ESP_OK;
      }

      esp_err_t Uart::ensure() {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        if (!uart_is_driver_installed(_defaults.port)) {
          esp_err_t err = uart_driver_install(_defaults.port, _defaults.rxBufSize,
                                              _defaults.txBufSize, 0, nullptr,
                                              ESP_INTR_FLAG_LOWMED);
          if (err != ESP_OK) {
            logE("uart_driver_install failed: %s", esp_err_to_name(err));
            return err;
          }
        }
#else
        {
          esp_err_t err = uart_driver_install(_defaults.port, _defaults.rxBufSize,
                                              _defaults.txBufSize, 0, nullptr,
                                              ESP_INTR_FLAG_LOWMED);
          if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            logE("uart_driver_install failed: %s", esp_err_to_name(err));
            return err;
          }
        }
#endif

        const esp_err_t err = applyConfig(_defaults);
        if (err != ESP_OK)
          return err;

        auto pinStr = [](int pin, char *buf, size_t len) -> const char * {
          if (pin == UART_PIN_NO_CHANGE)
            return "default";
          snprintf(buf, len, "%d", pin);
          return buf;
        };
        auto dataBitsStr = [](uart_word_length_t v) -> const char * {
          switch (v) {
            case UART_DATA_5_BITS:
              return "5";
            case UART_DATA_6_BITS:
              return "6";
            case UART_DATA_7_BITS:
              return "7";
            case UART_DATA_8_BITS:
              return "8";
            default:
              return "?";
          }
        };
        auto parityStr = [](uart_parity_t v) -> const char * {
          switch (v) {
            case UART_PARITY_DISABLE:
              return "N";
            case UART_PARITY_EVEN:
              return "E";
            case UART_PARITY_ODD:
              return "O";
            default:
              return "?";
          }
        };
        auto stopBitsStr = [](uart_stop_bits_t v) -> const char * {
          switch (v) {
            case UART_STOP_BITS_1:
              return "1";
            case UART_STOP_BITS_1_5:
              return "1.5";
            case UART_STOP_BITS_2:
              return "2";
            default:
              return "?";
          }
        };

        char txBuf[12]{};
        char rxBuf[12]{};
        logI("UART%d ready: TX %s, RX %s, %u %s%s%s", (int)_defaults.port,
             pinStr(_defaults.txGpio, txBuf, sizeof(txBuf)),
             pinStr(_defaults.rxGpio, rxBuf, sizeof(rxBuf)),
             static_cast<unsigned>(_defaults.baudrate), dataBitsStr(_defaults.dataBits),
             parityStr(_defaults.parity), stopBitsStr(_defaults.stopBits));
        return ESP_OK;
      }

      esp_err_t Uart::resetToDefaults() {
        const esp_err_t err = ensure();
        if (err != ESP_OK)
          return err;
        return applyConfig(_defaults);
      }

      esp_err_t Uart::apply(const UartConfig &cfg) {
        const esp_err_t err = ensure();
        if (err != ESP_OK)
          return err;
        return applyConfig(cfg);
      }

      esp_err_t Uart::setBaudrate(uint32_t baudrate) {
        if (!baudrate)
          return ESP_ERR_INVALID_ARG;
        const esp_err_t err = uart_set_baudrate(_defaults.port, baudrate);
        if (err != ESP_OK) {
          logE("uart_set_baudrate(%u) failed: %s", static_cast<unsigned>(baudrate),
               esp_err_to_name(err));
          return err;
        }
        return ESP_OK;
      }

      esp_err_t Uart::flushInput() {
        return uart_flush_input(_defaults.port);
      }

      esp_err_t Uart::waitTxDone(TickType_t timeout) {
        return uart_wait_tx_done(_defaults.port, timeout);
      }

      int Uart::read(uint8_t *buf, size_t len, TickType_t timeout) {
        const int n = uart_read_bytes(_defaults.port, buf, len, timeout);
        if (n > 0) {
          auto *tap = _rxTap.load();
          if (tap)
            tap->onRx(buf, static_cast<size_t>(n));
        }
        return n;
      }

      int Uart::write(const uint8_t *data, size_t len) {
        if (!data || !len)
          return 0;
        return uart_write_bytes(_defaults.port, reinterpret_cast<const char *>(data),
                                len);
      }

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
