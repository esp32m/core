#include <esp_modbus_master.h>

#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace modbus {

    static const char* TAG = "esp32m.modbus";

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
                                 uart_parity_t parity, bool ascii,
                                 uint8_t uartRxTimeout,
                                 uint32_t responseTimeoutMs) {
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
      if (_uartRxTimeout != uartRxTimeout) {
        _uartRxTimeout = uartRxTimeout;
        changed = true;
      }
      if (_responseTimeoutMs != responseTimeoutMs) {
        _responseTimeoutMs = responseTimeoutMs;
        changed = true;
      }

      _config.ser_opts.response_tout_ms = _responseTimeoutMs;
      _config.ser_opts.data_bits = UART_DATA_8_BITS;
      _config.ser_opts.stop_bits = UART_STOP_BITS_1;
      _mutex = &locks::uart(port);

      if (changed && _running) {
        stop();
        start();
      }
      _configured = true;
    }

    esp_err_t Master::startNoLock() {
      auto configurePinsAndDescriptor = [&]() -> esp_err_t {
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
        return ESP_OK;
      };

      if (!_handle) {
        logI("%s creating controller (port=%d baud=%u parity=%d mode=%d)", TAG,
             (int)_config.ser_opts.port, (unsigned)_config.ser_opts.baudrate,
             (int)_config.ser_opts.parity, (int)_config.ser_opts.mode);
        ESP_CHECK_RETURN(mbc_master_create_serial(&_config, &_handle));
      }

      ESP_CHECK_RETURN(configurePinsAndDescriptor());

      esp_err_t err = mbc_master_start(_handle);
      if (err == ESP_ERR_INVALID_STATE) {
        // Internally this is MB_EILLSTATE (0x6) from mb_base->enable().
        // Recover by deleting and recreating the controller.
        logW("%s mbc_master_start returned ESP_ERR_INVALID_STATE; recreating controller", TAG);
        (void)mbc_master_stop(_handle);
        (void)mbc_master_delete(_handle);
        _handle = nullptr;
        _running = false;
        ESP_CHECK_RETURN(mbc_master_create_serial(&_config, &_handle));
        ESP_CHECK_RETURN(configurePinsAndDescriptor());
        err = mbc_master_start(_handle);
      }
      ESP_CHECK_RETURN(err);

      ESP_CHECK_RETURN(
          uart_set_mode(_config.ser_opts.port, UART_MODE_RS485_HALF_DUPLEX));

      // esp-modbus sets a small UART RX timeout (in symbols). Some slaves
      // violate Modbus RTU inter-character timing and respond with long gaps
      // between bytes. In such case the port task sees multiple 1-2 byte
      // timeout fragments and drops them as "short packets", leading to
      // master request timeouts even though a valid response exists on the bus.
      uint8_t rxTimeout = _uartRxTimeout;
      // UART RX timeout threshold has a hardware-dependent maximum.
      // If we pass a too-large value, the UART driver rejects it (and logs
      // something like "tout_thresh = X > maximum value = Y").
      // Previously we used ESP_CHECK_RETURN here, which could make startNoLock
      // return an error while the Modbus stack was already started, leading to
      // repeated MB_EILLSTATE (0x6) on subsequent start attempts.
      if (rxTimeout > 100) {
        logW("%s uartRxTimeout=%u too high, clamping to 100", TAG,
             (unsigned)rxTimeout);
        rxTimeout = 100;
      }
      auto rxErr = uart_set_rx_timeout(_config.ser_opts.port, rxTimeout);
      if (rxErr != ESP_OK) {
        logW("%s uart_set_rx_timeout(%u) failed err=%d; using esp-modbus default",
             TAG, (unsigned)rxTimeout, (int)rxErr);
      }
      _running = true;
      return ESP_OK;
    }

    esp_err_t Master::start() {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      std::lock_guard guard(*_mutex);
      return startNoLock();
    }

    esp_err_t Master::stopNoLock() {
      if (!_handle) {
        _running = false;
        return ESP_OK;
      }
      auto err = mbc_master_stop(_handle);
      if (err == ESP_ERR_INVALID_STATE)
        err = ESP_OK;
      ESP_CHECK_RETURN(err);
      _running = false;
      return ESP_OK;
    }

    esp_err_t Master::stop() {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      std::lock_guard guard(*_mutex);
      return stopNoLock();
    }

    esp_err_t Master::resetNoLock() {
      (void)stopNoLock();
      // Some slaves can finish sending a response slightly late; give the UART
      // ISR/task a moment to drain/queue bytes, then flush twice to drop any
      // trailing fragments before restarting the stack.
      delay(5);
      (void)uart_flush_input(_config.ser_opts.port);
      delay(5);
      (void)uart_flush_input(_config.ser_opts.port);
      return startNoLock();
    }

    esp_err_t Master::request(uint8_t addr, Command command, uint16_t reg_start,
                              uint16_t reg_size, void* data) {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      std::lock_guard guard(*_mutex);
      if (!_running)
        ESP_CHECK_RETURN(startNoLock());
      mb_param_request_t req = {.slave_addr = addr,
                                .command = command,
                                .reg_start = reg_start,
                                .reg_size = reg_size};
      auto err = mbc_master_send_request(_handle, &req, data);
      if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_RESPONSE) {
        if (resetNoLock() == ESP_OK)
          err = mbc_master_send_request(_handle, &req, data);
      }
      return err;
    }

    namespace master {
      void configureSerial(uart_port_t port, uint32_t baud,
                           uart_parity_t parity, bool ascii,
                           uint8_t uartRxTimeout,
                           uint32_t responseTimeoutMs) {
        Master::instance().configureSerial(port, baud, parity, ascii,
                                           uartRxTimeout, responseTimeoutMs);
      }
    }  // namespace master

  }  // namespace modbus
}  // namespace esp32m