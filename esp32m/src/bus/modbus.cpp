#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

#include <driver/uart.h>
#include <esp_timer.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

namespace esp32m {
  namespace modbus {

    static const char* TAG = "esp32m.modbus";

    Master& Master::instance() {
      static Master i;
      return i;
    }

    void Master::observe(bool raw, uint8_t address, esp_err_t result,
                         uint64_t durationUs, bool validated) {
      auto* observer = _observer.load();
      if (!observer) return;
      const auto outcome = result == ESP_OK
          ? (validated ? Outcome::Success : Outcome::Unverified)
          : result == ESP_ERR_TIMEOUT ? Outcome::Timeout
          : (result == ESP_ERR_INVALID_RESPONSE || result == ESP_ERR_INVALID_SIZE)
              ? Outcome::InvalidResponse : Outcome::OtherError;
      observer->onTransaction({raw, address, outcome, durationUs});
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

    esp_err_t Master::configurePinsNoLock() {
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
#if CONFIG_IDF_TARGET_ESP32S3
          txp = 13;
          rxp = 14;
#endif
          break;
#endif
        default:
          return ESP_FAIL;
      }
      ESP_CHECK_RETURN(uart_set_pin(_config.ser_opts.port, txp, rxp,
                                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
      return ESP_OK;
    }

    esp_err_t Master::startNoLock() {
      auto configurePinsAndDescriptor = [&]() -> esp_err_t {
        ESP_CHECK_RETURN(configurePinsNoLock());

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
      if (_recoveryPending)
        return resetNoLock();
      return _running ? ESP_OK : startNoLock();
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
      // stop/start only disables reception; it retains the Modbus event queue,
      // completion bits and UART task. A late completion can therefore finish
      // the next slave's request immediately. Destroy all of those together.
      // Delete the active controller directly: stopping it first also makes
      // esp-modbus 2.1.3's delete wait for an already-cleared STARTED bit.
      if (_handle) {
        ESP_CHECK_RETURN(mbc_master_delete(_handle));
        _handle = nullptr;
        _running = false;
      }
      delay(10);
      const auto error = startNoLock();
      if (error == ESP_OK) _recoveryPending = false;
      return error;
    }

    esp_err_t Master::request(uint8_t addr, Command command, uint16_t reg_start,
                              uint16_t reg_size, void* data, bool retry,
                              uint32_t quietBeforeMs) {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      if (!data || !reg_size)
        return ESP_ERR_INVALID_ARG;
      const bool packedBits = command == ReadCoils || command == ReadDiscrete ||
                              command == WriteCoils;
      const size_t dataSize = packedBits ? (size_t(reg_size) + 7) / 8
                                        : size_t(reg_size) * sizeof(uint16_t);
      if (dataSize > sizeof(_requestData))
        return ESP_ERR_INVALID_SIZE;
      std::lock_guard guard(*_mutex);
      auto preparation = _recoveryPending ? resetNoLock() : ESP_OK;
      if (preparation == ESP_OK && !_running) preparation = startNoLock();
      if (preparation != ESP_OK) {
        observe(false, addr, preparation, 0);
        return preparation;
      }
      mb_param_request_t req = {};
      req.slave_addr = addr;
      req.command = command;
      req.reg_start = reg_start;
      req.reg_size = reg_size;
      auto send = [&](unsigned attempt) {
        // A delay in the device driver would not reserve a quiet bus: another
        // task could transmit during it. Keep the interval and TX under the
        // same UART lock, after any controller startup/recovery.
        if (quietBeforeMs) {
          // Tick delays can end early relative to wall time: at 100 Hz,
          // delay(12) is one tick and may wait only a fraction of 10 ms.
          // Check a monotonic deadline and always block for at least one tick
          // while time remains, including guards shorter than a single tick.
          const auto deadline = esp_timer_get_time() + int64_t(quietBeforeMs) * 1000;
          for (;;) {
            const auto remainingUs = deadline - esp_timer_get_time();
            if (remainingUs <= 0) break;
            const auto remainingMs = std::min<int64_t>((remainingUs + 999) / 1000, 1000);
            vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(remainingMs)));
          }
        }
        std::memcpy(_requestData, data, dataSize);
        const auto started = esp_timer_get_time();
        const auto error = mbc_master_send_request(_handle, &req, _requestData);
        const auto duration = esp_timer_get_time() - started;
        // Broadcasts do not require a slave reply; a successful send alone
        // must not improve the communication-health score.
        observe(false, addr, error, duration, addr != 0);
        if (error != ESP_OK)
          logW("request failed: address=%u function=0x%02x register=0x%04x count=%u attempt=%u error=%d elapsed=%llums",
               unsigned(addr), unsigned(command), unsigned(reg_start),
               unsigned(reg_size), attempt, int(error),
               static_cast<unsigned long long>(duration / 1000));
        return error;
      };
      esp_err_t err = ESP_FAIL;
      const unsigned attempts = retry ? 2 : 1;
      for (unsigned attempt = 1; attempt <= attempts; ++attempt) {
        err = send(attempt);
        if (err != ESP_ERR_TIMEOUT && err != ESP_ERR_INVALID_RESPONSE)
          break;
        // Recover after the final failed attempt too, before another slave or
        // a single-shot write caller gets the bus. Never retransmit here when
        // retry=false; the caller must independently verify uncertain writes.
        _recoveryPending = true;
        const auto recovery = resetNoLock();
        if (recovery != ESP_OK) {
          logW("controller recovery failed: address=%u requestError=%d recoveryError=%d",
               unsigned(addr), int(err), int(recovery));
          break;
        }
      }
      if (err == ESP_OK)
        std::memcpy(data, _requestData, dataSize);
      return err;
    }

    esp_err_t Master::transactRaw(const void* request, size_t requestSize,
                                  void* response, size_t responseCapacity,
                                  size_t* responseSize,
                                  uint32_t responseTimeoutMs) {
      if (!_mutex)
        return ESP_ERR_INVALID_STATE;
      if (!request || !requestSize || !response || !responseCapacity ||
          !responseSize)
        return ESP_ERR_INVALID_ARG;

      std::lock_guard guard(*_mutex);
      uint64_t duration = 0;
      auto execute = [&]() -> esp_err_t {
        *responseSize = 0;

        const bool wasRunning = _running;
        if (!_running)
          ESP_CHECK_RETURN(startNoLock());
        ESP_CHECK_RETURN(stopNoLock());

        const auto port = _config.ser_opts.port;
        esp_err_t result = uart_flush_input(port);
        const auto began = esp_timer_get_time();
        if (result == ESP_OK) {
          const int written = uart_write_bytes(
              port, static_cast<const char*>(request), requestSize);
          if (written != static_cast<int>(requestSize))
            result = ESP_FAIL;
        }

        const uint32_t timeoutMs =
            responseTimeoutMs ? responseTimeoutMs : _responseTimeoutMs;
        TickType_t timeout = pdMS_TO_TICKS(timeoutMs);
        if (!timeout)
          timeout = 1;
        if (result == ESP_OK)
          result = uart_wait_tx_done(port, timeout);
        if (result == ESP_OK) {
          const int received = uart_read_bytes(
              port, response, responseCapacity, timeout);
          if (received > 0)
            *responseSize = static_cast<size_t>(received);
          else
            result = received == 0 ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }

        duration = esp_timer_get_time() - began;
        if (wasRunning) {
          const auto restartResult = startNoLock();
          if (result == ESP_OK)
            result = restartResult;
        }
        return result;
      };
      const auto result = execute();
      observe(true, 0, result, duration, false);
      return result;
    }

    esp_err_t Master::exchangeRaw(
        const void* request, size_t requestSize, void* response,
        size_t responseCapacity, size_t* responseSize, uint32_t baud,
        uint32_t responseTimeoutMs,
        bool (*complete)(const uint8_t*, size_t, void*), void* context) {
      if (responseSize)
        *responseSize = 0;
      if (!request || !requestSize || requestSize > 512 || !response ||
          !responseCapacity || responseCapacity > 1024 || !responseSize ||
          !responseTimeoutMs || responseTimeoutMs > 60000)
        return ESP_ERR_INVALID_ARG;
      if (!_mutex || !_configured)
        return ESP_ERR_INVALID_STATE;

      std::lock_guard guard(*_mutex);
      uint64_t duration = 0;
      auto execute = [&]() -> esp_err_t {
        const bool wasRunning = _running;
        const auto port = _config.ser_opts.port;
        // stop() alone leaves a UART event task that can still consume/flush a
        // queued event before noticing its disabled flag. Deletion joins that
        // ownership boundary and removes its driver before raw installation.
        if (_handle) {
          ESP_CHECK_RETURN(mbc_master_delete(_handle));
          _handle = nullptr;
          _running = false;
        }
        if (uart_is_driver_installed(port))
          return ESP_ERR_INVALID_STATE;

        bool rawInstalled = false;
        int64_t wireStarted = 0;
        bool sent = false;
        auto exchange = [&]() -> esp_err_t {
          uart_config_t config = {};
          config.baud_rate = baud ? baud : _config.ser_opts.baudrate;
          config.data_bits = UART_DATA_8_BITS;
          config.parity = UART_PARITY_DISABLE;
          config.stop_bits = UART_STOP_BITS_1;
          config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
          config.source_clk = UART_SCLK_DEFAULT;
          ESP_CHECK_RETURN(uart_param_config(port, &config));
          // TX buffer comfortably exceeds our maximum single request; no
          // unbounded producer wait for buffer space during this sole write.
          ESP_CHECK_RETURN(uart_driver_install(port, 2048, 2048, 0, nullptr, 0));
          rawInstalled = true;
          ESP_CHECK_RETURN(configurePinsNoLock());
          ESP_CHECK_RETURN(uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX));
          delay(150);
          ESP_CHECK_RETURN(uart_flush_input(port));

          wireStarted = esp_timer_get_time();
          sent = true;
          const int64_t deadline = wireStarted + int64_t(responseTimeoutMs) * 1000;
          auto remainingTicks = [&](uint32_t limitMs) -> TickType_t {
            const auto us = deadline - esp_timer_get_time();
            if (us <= 0)
              return 0;
            const auto ms = std::min<uint32_t>((us + 999) / 1000, limitMs);
            return std::max<TickType_t>(1, pdMS_TO_TICKS(ms));
          };
          if (uart_write_bytes(port, request, requestSize) != int(requestSize))
            return ESP_FAIL;
          auto wait = remainingTicks(responseTimeoutMs);
          if (!wait)
            return ESP_ERR_TIMEOUT;
          ESP_CHECK_RETURN(uart_wait_tx_done(port, wait));
          auto* bytes = static_cast<uint8_t*>(response);
          while ((wait = remainingTicks(20)) != 0) {
            // IDF applies ticks_to_wait separately to every ring-buffer
            // fragment, not to the whole uart_read_bytes call. Reading one
            // byte bounds each wait even under a continuous slow/noisy stream.
            const int count = uart_read_bytes(
                port, bytes + *responseSize, 1, wait);
            if (count < 0)
              return ESP_FAIL;
            *responseSize += count;
            if (count && complete && complete(bytes, *responseSize, context))
              return ESP_OK;
            if (*responseSize == responseCapacity)
              return complete ? ESP_ERR_INVALID_SIZE : ESP_OK;
          }
          return !complete && *responseSize ? ESP_OK : ESP_ERR_TIMEOUT;
        };

        auto result = exchange();
        if (sent) duration = esp_timer_get_time() - wireStarted;
        // Keep late/truncated replies separated from the restored RTU receiver.
        delay(150);
        if (rawInstalled) {
          const auto cleanup = uart_driver_delete(port);
          if (cleanup != ESP_OK) {
            logE("%s raw UART cleanup failed err=%d", TAG, int(cleanup));
            // Never create a second owner over a driver that failed to close.
            return cleanup;
          }
        }
        if (wasRunning) {
          const auto restore = startNoLock();
          if (restore != ESP_OK) {
            logE("%s restoring controller after raw exchange failed err=%d", TAG,
                 int(restore));
            return restore;
          }
        }
        return result;
      };
      const auto result = execute();
      observe(true, 0, result, duration, complete != nullptr);
      return result;
    }

    esp_err_t Master::setBaudrate(uint32_t baud) {
      if (!baud)
        return ESP_ERR_INVALID_ARG;
      if (!_mutex || !_configured)
        return ESP_ERR_INVALID_STATE;

      std::lock_guard guard(*_mutex);
      if (_config.ser_opts.baudrate == baud)
        return ESP_OK;

      const bool wasRunning = _running;
      if (_handle) {
        ESP_CHECK_RETURN(stopNoLock());
        ESP_CHECK_RETURN(mbc_master_delete(_handle));
        _handle = nullptr;
      }
      _config.ser_opts.baudrate = baud;
      return wasRunning ? startNoLock() : ESP_OK;
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
