#include <esp_modbus_master.h>

#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace modbus {

    Master& Master::instance() {
      static Master i;
      return i;
    }

    mb_parameter_descriptor_t dummy_device_parameters[] = {
        // CID, Name, Units, Modbus addr, register type, Modbus Reg Start
        // Addr, Modbus Reg read length,
        {0, "--", "--", 1, MB_PARAM_INPUT, 0, 2, 0, PARAM_TYPE_U32, 4, {},
         PAR_PERMS_READ_WRITE_TRIGGER},
    };

    void Master::configureSerial(uart_port_t port, uint32_t baud,
                                 uart_parity_t parity, bool ascii) {
      auto mode = ascii ? MB_ASCII : MB_RTU;
      bool changed = false;
      auto& ser = _config.ser_opts;
      if (ser.mode != mode) {
        ser.mode = mode;
        changed = true;
      }
      if (ser.port != port) {
        ser.port = port;
        changed = true;
      }
      if (ser.baudrate != baud) {
        ser.baudrate = baud;
        changed = true;
      }
      if (ser.parity != parity) {
        ser.parity = parity;
        changed = true;
      }
      _mutex = &locks::uart(port);
      if (changed && _running) {
        stop();
        start();
      }
      _config.ser_opts.response_tout_ms = 1000,
      _config.ser_opts.data_bits = UART_DATA_8_BITS,
      _config.ser_opts.stop_bits = UART_STOP_BITS_1;
      _configured = true;
    }

    esp_err_t Master::start() {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      std::lock_guard guard(*_mutex);
      // ESP_CHECK_RETURN(mbc_master_init(MB_PORT_SERIAL_MASTER, &_handle));
      // ESP_CHECK_RETURN(mbc_master_setup((void*)&_config));
      if (!_handle)
        ESP_CHECK_RETURN(mbc_master_create_serial(&_config, &_handle));
      int txp = 17, rxp = 16;
      switch (_config.ser_opts.port) {
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
      ESP_CHECK_RETURN(uart_set_pin(_config.ser_opts.port, txp, rxp,
                                    UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

      // it appears that mbc_master_set_descriptor must be called even if we
      // don't use device parameters
      ESP_CHECK_RETURN(
          mbc_master_set_descriptor(_handle, &dummy_device_parameters[0], 1));

      ESP_CHECK_RETURN(mbc_master_start(_handle));
      ESP_CHECK_RETURN(
          uart_set_mode(_config.ser_opts.port, UART_MODE_RS485_HALF_DUPLEX));
      _running = true;
      return ESP_OK;
    }

    esp_err_t Master::stop() {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      std::lock_guard guard(*_mutex);
      ESP_CHECK_RETURN(mbc_master_stop(_handle));
      _running = false;
      return ESP_OK;
    }

    esp_err_t Master::request(uint8_t addr, Command command, uint16_t reg_start,
                              uint16_t reg_size, void* data) {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      std::lock_guard guard(*_mutex);
      mb_param_request_t req = {.slave_addr = addr,
                                .command = command,
                                .reg_start = reg_start,
                                .reg_size = reg_size};
      return mbc_master_send_request(_handle, &req, data);
    }

    namespace master {
      void configureSerial(uart_port_t port, uint32_t baud,
                           uart_parity_t parity, bool ascii) {
        Master::instance().configureSerial(port, baud, parity, ascii);
      }
    }  // namespace master

  }  // namespace modbus
}  // namespace esp32m