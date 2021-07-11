#pragma once

#include <mutex>

#include <esp_modbus_common.h>
#include <hal/uart_types.h>

namespace esp32m {
  namespace modbus {
    enum Command {
      None = 0,
      ReadCoils = 1,
      ReadDiscrete = 2,
      ReadHolding = 3,
      ReadInput = 4,
      WriteCoil = 5,
      WriteRegister = 6,
      DiagRead = 7,
      Diag = 8,
      DiagComCnt = 11,
      DiagComLog = 12,
      WriteCoils = 15,
      WriteRegisters = 16,
      ReportId = 17,
      ReadWriteRegisters = 23,
    };

    class Modbus {
     public:
      virtual esp_err_t start() = 0;
      virtual esp_err_t stop() = 0;
      const mb_communication_info_t &config() const {
        return _config;
      }
      bool isConfigured() const {
        return _configured;
      }
      bool isRunning() const {
        return _running;
      }

     protected:
      std::mutex *_mutex;
      void *_handle = nullptr;
      mb_communication_info_t _config = {};
      bool _configured = false, _running = false;
    };

    class Master : public Modbus {
     public:
      static Master &instance();
      void configureSerial(uart_port_t port, uint32_t baud = 115200,
                           uart_parity_t parity = UART_PARITY_DISABLE,
                           bool ascii = false);

      esp_err_t start() override;
      esp_err_t stop() override;
      esp_err_t request(uint8_t addr, Command command, uint16_t reg_start,
                        uint16_t reg_size, void *data);

     private:
      Master() {}
    };

    class Slave : public Modbus {
     public:
    };

    namespace master {
      void configureSerial(uart_port_t port, uint32_t baud = 115200,
                           uart_parity_t parity = UART_PARITY_DISABLE,
                           bool ascii = false);
    }

  }  // namespace modbus
}  // namespace esp32m