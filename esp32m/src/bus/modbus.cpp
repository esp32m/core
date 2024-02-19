#include <esp_modbus_master.h>

#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace modbus {

    Master &Master::instance() {
      static Master i;
      return i;
    }

    void Master::configureSerial(uart_port_t port, uint32_t baud,
                                 uart_parity_t parity, bool ascii) {
      auto mode = ascii ? MB_MODE_ASCII : MB_MODE_RTU;
      bool changed = false;
      if (_config.mode != mode) {
        _config.mode = mode;
        changed = true;
      }
      if (_config.port != port) {
        _config.port = port;
        changed = true;
      }
      if (_config.baudrate != baud) {
        _config.baudrate = baud;
        changed = true;
      }
      if (_config.parity != parity) {
        _config.parity = parity;
        changed = true;
      }
      _mutex = &locks::uart(port);
      if (changed && _running) {
        stop();
        start();
      }
      _configured = true;
    }

    esp_err_t Master::start() {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      std::lock_guard guard(*_mutex);
      ESP_CHECK_RETURN(mbc_master_init(MB_PORT_SERIAL_MASTER, &_handle));
      ESP_CHECK_RETURN(mbc_master_setup((void *)&_config));
      int txp = 17, rxp = 16;
      switch (_config.port) {
        case UART_NUM_0:
          txp = 1;
          rxp = 3;
          break;
        case UART_NUM_1:
          txp = 9;
          rxp = 10;
          break;
#if SOC_UART_HP_NUM > 2
        case UART_NUM_2:
          break;
#endif
        default:
          return ESP_FAIL;
      }
      ESP_CHECK_RETURN(uart_set_pin(_config.port, txp, rxp, UART_PIN_NO_CHANGE,
                                    UART_PIN_NO_CHANGE));
      ESP_CHECK_RETURN(mbc_master_start());
      ESP_CHECK_RETURN(
          uart_set_mode(_config.port, UART_MODE_RS485_HALF_DUPLEX));
      _running = true;
      return ESP_OK;
    }

    esp_err_t Master::stop() {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      std::lock_guard guard(*_mutex);
      ESP_CHECK_RETURN(mbc_master_destroy());
      _running = false;
      return ESP_OK;
    }

    esp_err_t Master::request(uint8_t addr, Command command, uint16_t reg_start,
                              uint16_t reg_size, void *data) {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      std::lock_guard guard(*_mutex);
      mb_param_request_t req = {.slave_addr = addr,
                                .command = command,
                                .reg_start = reg_start,
                                .reg_size = reg_size};
      return mbc_master_send_request(&req, data);
    }

    namespace master {
      void configureSerial(uart_port_t port, uint32_t baud,
                           uart_parity_t parity, bool ascii) {
        Master::instance().configureSerial(port, baud, parity, ascii);
      }
    }  // namespace master

  }  // namespace modbus
}  // namespace esp32m