#pragma once
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include "esp32m/bus/diagnostics.hpp"
#include "esp32m/logging.hpp"

#include <hal/uart_types.h>
#include "mbcontroller.h"

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

    class Modbus : public log::Loggable {
     public:
      virtual esp_err_t start() = 0;
      virtual esp_err_t stop() = 0;
      const mb_communication_info_t& config() const {
        return _config;
      }
      bool isConfigured() const {
        return _configured;
      }
      bool isRunning() const {
        return _running;
      }

     protected:
      std::mutex* _mutex;
      void* _handle = nullptr;
      mb_communication_info_t _config = {};
      bool _configured = false, _running = false;
    };

    class Master : public Modbus {
     public:
      static Master& instance();
      // Observer must outlive the master. Registration does not touch the bus.
      void setObserver(Observer* observer) { _observer.store(observer); }
      const char* name() const override {
        return "modbus_master";
      }
      void configureSerial(uart_port_t port, uint32_t baud = 115200,
                           uart_parity_t parity = UART_PARITY_DISABLE,
                           bool ascii = false, uint8_t uartRxTimeout = 3,
                           uint32_t responseTimeoutMs = 3000);

      esp_err_t start() override;
      esp_err_t stop() override;
      /** Set retry=false for writes whose outcome must be verified before
       * another transmission. Failed transfers still resynchronize the bus.
       * Data is staged in controller-owned storage (up to 512 bytes) and
       * copied back only on success; late callbacks cannot access the caller.
       * quietBeforeMs reserves an additional quiet interval under the UART
       * mutex before each transmission, including a retry. Use it for slaves
       * that need extra recovery time after traffic addressed to other units.
       * A monotonic deadline guarantees at least that much elapsed time;
       * scheduler tick rounding may extend the interval, never shorten it.
       */
      esp_err_t request(uint8_t addr, Command command, uint16_t reg_start,
                        uint16_t reg_size, void* data, bool retry = true,
                        uint32_t quietBeforeMs = 0);

      /** Perform a raw serial transaction while the Modbus controller is
       * paused. The caller supplies and validates the complete RTU frames,
       * including CRC. This is intended for vendor commands that cannot be
       * represented by mb_param_request_t.
       */
      esp_err_t transactRaw(const void* request, size_t requestSize,
                            void* response, size_t responseCapacity,
                            size_t* responseSize,
                            uint32_t responseTimeoutMs = 0);

      /** Exchange one complete proprietary frame at 8N1, with a temporary
       * baud override (zero uses the configured baud). Owns the UART mutex,
       * removes the Modbus UART task, and restores the original running state
       * and configuration before returning. Never retries transmission.
       *
       * The optional non-blocking callback receives all bytes collected so
       * far, including any preamble/noise, and returns true on a validated
       * complete response. With no callback, reads until capacity/deadline.
       * A partial response is retained on timeout. Limits: 512 request bytes,
       * 1024 response bytes, and 60 seconds for TX plus RX. A 150 ms quiet
       * interval is held before TX and before returning the bus to Modbus.
       */
      esp_err_t exchangeRaw(
          const void* request, size_t requestSize, void* response,
          size_t responseCapacity, size_t* responseSize, uint32_t baud = 0,
          uint32_t responseTimeoutMs = 1500,
          bool (*complete)(const uint8_t*, size_t, void*) = nullptr,
          void* context = nullptr);

      /** Change the serial baud rate, recreating a running controller so the
       * new communication settings take effect immediately.
       */
      esp_err_t setBaudrate(uint32_t baud);

     private:
      Master() {}

      uint8_t _uartRxTimeout = 3;
      uint32_t _responseTimeoutMs = 3000;
      // esp-modbus retains its callback buffer after send_request returns.
      // Keep it alive even if an API timeout precedes the wire timeout or
      // controller teardown fails. Access remains under the UART mutex.
      uint16_t _requestData[256] = {};
      bool _recoveryPending = false;
      std::atomic<Observer*> _observer{nullptr};
      void observe(bool raw, uint8_t address, esp_err_t result,
                   uint64_t durationUs, bool validated = true);

      esp_err_t startNoLock();
      esp_err_t stopNoLock();
      esp_err_t resetNoLock();
      esp_err_t configurePinsNoLock();
    };

    class Slave : public Modbus {
     public:
    };

    namespace master {
      void configureSerial(uart_port_t port, uint32_t baud = 115200,
                           uart_parity_t parity = UART_PARITY_DISABLE,
                           bool ascii = false, uint8_t uartRxTimeout = 3,
                           uint32_t responseTimeoutMs = 3000);
    }

  }  // namespace modbus
}  // namespace esp32m
