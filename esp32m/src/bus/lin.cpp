#include "esp32m/bus/lin.hpp"
#include "esp32m/defs.hpp"

#include <cstring>
#include <driver/uart.h>

namespace esp32m {
  namespace lin {

    static constexpr int RxBufSize      = 256;
    static constexpr int TxBufSize      = 128;
    static constexpr int EventQueueSize = 8;

    // -----------------------------------------------------------------------
    // Protocol helpers
    // -----------------------------------------------------------------------

    uint8_t computePid(uint8_t id) {
      id &= 0x3F;
      // P0 = ID0 ^ ID1 ^ ID2 ^ ID4
      const uint8_t p0 =
          ((id >> 0) ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 1u;
      // P1 = ~(ID1 ^ ID3 ^ ID4 ^ ID5)
      const uint8_t p1 =
          (~((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5))) & 1u;
      return static_cast<uint8_t>(id | (p0 << 6) | (p1 << 7));
    }

    uint8_t computeChecksum(uint8_t pid, const uint8_t *data, uint8_t len,
                            Checksum cs) {
      // One's-complement sum with carry folding.
      uint16_t sum = (cs == Checksum::Enhanced) ? pid : 0u;
      for (uint8_t i = 0; i < len; ++i) {
        sum += data[i];
        if (sum > 0xFF)
          sum -= 0xFF;  // fold the carry back into the low byte
      }
      return static_cast<uint8_t>(~sum);
    }

    // -----------------------------------------------------------------------
    // Lin base
    // -----------------------------------------------------------------------

    static esp_err_t applyUartConfig(uart_port_t port, int txPin, int rxPin,
                                     uint32_t baud) {
      uart_config_t cfg   = {};
      cfg.baud_rate       = static_cast<int>(baud);
      cfg.data_bits       = UART_DATA_8_BITS;
      cfg.parity          = UART_PARITY_DISABLE;
      cfg.stop_bits       = UART_STOP_BITS_1;
      cfg.flow_ctrl       = UART_HW_FLOWCTRL_DISABLE;
      cfg.source_clk      = UART_SCLK_DEFAULT;
      ESP_CHECK_RETURN(uart_param_config(port, &cfg));
      ESP_CHECK_RETURN(uart_set_pin(port, txPin, rxPin,
                                    UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
      return ESP_OK;
    }

    Lin::Lin(uart_port_t port, int txPin, int rxPin, uint32_t baud)
        : _port(port), _txPin(txPin), _rxPin(rxPin), _baud(baud) {}

    Lin::~Lin() {
      destroyDriver();
    }

    void Lin::destroyDriver() {
      if (_driverInstalled) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(uart_driver_delete(_port));
        _driverInstalled = false;
      }
    }

    esp_err_t Lin::sendBreak() {
      // Transmit 0x00 at half baud.
      // One byte = start(1) + 8×zero + stop(1) = 10 bits at baud/2
      //          = 20 bit-times at full baud  ≥  13-bit LIN break minimum.
      // The stop bit at baud/2 provides the mandatory break delimiter.
      ESP_CHECK_RETURN(uart_set_baudrate(_port, _baud / 2));
      const uint8_t zero = 0x00;
      uart_write_bytes(_port, &zero, 1);
      // Allow extra headroom for 9600 baud (≈2 ms per byte at baud/2 = 4800).
      ESP_CHECK_RETURN(uart_wait_tx_done(_port, pdMS_TO_TICKS(20)));
      ESP_CHECK_RETURN(uart_set_baudrate(_port, _baud));
      return ESP_OK;
    }

    void Lin::flushRx() {
      uart_flush_input(_port);
    }

    // -----------------------------------------------------------------------
    // Master
    // -----------------------------------------------------------------------

    Master::Master(uart_port_t port, int txPin, int rxPin, uint32_t baud,
                   RxCallback rxCallback)
        : Lin(port, txPin, rxPin, baud), _rxCallback(std::move(rxCallback)) {
      if (applyUartConfig(port, txPin, rxPin, baud) != ESP_OK)
        return;
      // Master does not require a UART event queue; use the simple
      // buffered driver with independent TX and RX ring buffers.
      if (uart_driver_install(port, RxBufSize, TxBufSize, 0, nullptr, 0) !=
          ESP_OK) {
        logE("uart_driver_install failed");
        return;
      }
      _driverInstalled = true;
    }

    esp_err_t Master::sendHeader(uint8_t pid) {
      // Clear any stale RX data before we start a new frame.
      flushRx();
      ESP_CHECK_RETURN(sendBreak());

      const uint8_t hdr[2] = {SyncByte, pid};
      if (uart_write_bytes(_port, hdr, sizeof(hdr)) !=
          static_cast<int>(sizeof(hdr)))
        return ESP_ERR_INVALID_SIZE;
      ESP_CHECK_RETURN(uart_wait_tx_done(_port, pdMS_TO_TICKS(10)));

      // Discard UART echo bytes: the LIN transceiver (e.g. TJA1021) mirrors
      // whatever it drives onto the bus back to RXD.  By the time TX is done
      // all echo bytes have been clocked into the RX FIFO.
      flushRx();
      return ESP_OK;
    }

    esp_err_t Master::publish(const Frame &frame, const uint8_t *data,
                               int timeoutMs) {
      if (frame.dataLen == 0 || frame.dataLen > MaxDataLen)
        return ESP_ERR_INVALID_ARG;

      std::lock_guard lock(_mutex);

      const uint8_t pid = computePid(frame.id);
      ESP_CHECK_RETURN(sendHeader(pid));

      // Append checksum so the entire payload goes out in one write.
      uint8_t buf[MaxDataLen + 1];
      memcpy(buf, data, frame.dataLen);
      buf[frame.dataLen] =
          computeChecksum(pid, data, frame.dataLen, frame.checksum);

      if (uart_write_bytes(_port, buf, frame.dataLen + 1) !=
          frame.dataLen + 1)
        return ESP_ERR_INVALID_SIZE;
      ESP_CHECK_RETURN(uart_wait_tx_done(_port, pdMS_TO_TICKS(timeoutMs)));
      return ESP_OK;
    }

    esp_err_t Master::request(const Frame &frame, int timeoutMs) {
      if (frame.dataLen == 0 || frame.dataLen > MaxDataLen)
        return ESP_ERR_INVALID_ARG;
      if (!_rxCallback)
        return ESP_ERR_INVALID_STATE;

      std::lock_guard lock(_mutex);

      const uint8_t pid = computePid(frame.id);
      ESP_CHECK_RETURN(sendHeader(pid));

      // Read data bytes + checksum byte from the slave.
      uint8_t buf[MaxDataLen + 1];
      const int toRead   = frame.dataLen + 1;
      const int received =
          uart_read_bytes(_port, buf, toRead, pdMS_TO_TICKS(timeoutMs));
      if (received != toRead) {
        logW("request id=0x%02x: expected %d bytes, got %d",
             (unsigned)frame.id, toRead, received);
        return ESP_ERR_TIMEOUT;
      }

      const uint8_t expected =
          computeChecksum(pid, buf, frame.dataLen, frame.checksum);
      if (buf[frame.dataLen] != expected) {
        logW("request id=0x%02x: checksum 0x%02x != expected 0x%02x",
             (unsigned)frame.id, (unsigned)buf[frame.dataLen],
             (unsigned)expected);
        return ESP_ERR_INVALID_CRC;
      }

      _rxCallback(pid, buf, frame.dataLen);
      return ESP_OK;
    }

    // -----------------------------------------------------------------------
    // Slave
    // -----------------------------------------------------------------------

    Slave::Slave(uart_port_t port, int txPin, int rxPin, uint32_t baud)
        : Lin(port, txPin, rxPin, baud) {
      if (applyUartConfig(port, txPin, rxPin, baud) != ESP_OK)
        return;
      // Install the UART driver with an event queue so the background task
      // can block on UART_BREAK events rather than polling.
      if (uart_driver_install(port, RxBufSize, TxBufSize,
                              EventQueueSize, &_uartEventQueue, 0) != ESP_OK) {
        logE("uart_driver_install failed");
        return;
      }
      _driverInstalled = true;
      xTaskCreate(
          [](void *self) { static_cast<Slave *>(self)->run(); },
          "m/lin/slave", 3072, this, 5, &_task);
    }

    Slave::~Slave() {
      // Delete the UART driver first so it can no longer post events to
      // _uartEventQueue before we destroy it.
      destroyDriver();
      if (_task) {
        vTaskDelete(_task);
        _task = nullptr;
      }
      if (_uartEventQueue) {
        vQueueDelete(_uartEventQueue);
        _uartEventQueue = nullptr;
      }
    }

    void Slave::setTxHandler(const Frame &frame, TxHandler handler) {
      std::lock_guard lock(_mutex);
      auto &reg     = _registrations[frame.id & 0x3F];
      reg.frame     = frame;
      reg.txHandler = std::move(handler);
    }

    void Slave::setRxHandler(const Frame &frame, RxHandler handler) {
      std::lock_guard lock(_mutex);
      auto &reg     = _registrations[frame.id & 0x3F];
      reg.frame     = frame;
      reg.rxHandler = std::move(handler);
    }

    void Slave::run() {
      uart_event_t event;
      for (;;) {
        if (xQueueReceive(_uartEventQueue, &event, portMAX_DELAY) != pdTRUE)
          continue;

        // Recover from buffer overflow so we don't process stale data.
        if (event.type == UART_FIFO_OVF ||
            event.type == UART_BUFFER_FULL) {
          logW("RX overflow — flushing");
          uart_flush_input(_port);
          xQueueReset(_uartEventQueue);
          continue;
        }

        if (event.type != UART_BREAK)
          continue;

        // After a break event the driver may or may not have placed the break
        // null byte (0x00) into the RX ring buffer before our task wakes.  Read
        // up to 3 bytes and search for the sync byte (0x55) to handle both
        // cases robustly:
        //   With null byte in buffer:    [0x00, 0x55, pid]
        //   Without null byte in buffer: [0x55, pid,  ...]
        uint8_t buf[3] = {};
        const int n =
            uart_read_bytes(_port, buf, sizeof(buf), pdMS_TO_TICKS(10));

        int si = -1;
        for (int i = 0; i < n; ++i) {
          if (buf[i] == SyncByte) {
            si = i;
            break;
          }
        }
        if (si < 0 || si + 1 >= n) {
          logW("no LIN sync after break (n=%d)", n);
          continue;
        }

        const uint8_t pid = buf[si + 1];
        const uint8_t id  = pid & 0x3F;
        if (computePid(id) != pid) {
          logW("bad PID parity: received 0x%02x, expected 0x%02x",
               (unsigned)pid, (unsigned)computePid(id));
          continue;
        }

        // Copy the registration while holding the mutex, then release before
        // calling the handler to avoid holding the lock during user callbacks.
        std::unique_lock lock(_mutex);
        auto it = _registrations.find(id);
        if (it == _registrations.end())
          continue;
        Registration reg = it->second;
        lock.unlock();

        if (reg.txHandler) {
          // Master is requesting data from us.
          uint8_t data[MaxDataLen] = {};
          if (reg.txHandler(pid, data, reg.frame.dataLen)) {
            uint8_t out[MaxDataLen + 1];
            memcpy(out, data, reg.frame.dataLen);
            out[reg.frame.dataLen] =
                computeChecksum(pid, data, reg.frame.dataLen, reg.frame.checksum);
            uart_write_bytes(_port, out, reg.frame.dataLen + 1);
          }
        } else if (reg.rxHandler) {
          // Master is publishing data; read the payload + checksum.
          uint8_t rxBuf[MaxDataLen + 1];
          const int toRead = reg.frame.dataLen + 1;
          const int rxn =
              uart_read_bytes(_port, rxBuf, toRead, pdMS_TO_TICKS(20));
          if (rxn != toRead) {
            logW("rx id=0x%02x: expected %d bytes, got %d",
                 (unsigned)id, toRead, rxn);
            continue;
          }
          const uint8_t expected =
              computeChecksum(pid, rxBuf, reg.frame.dataLen, reg.frame.checksum);
          if (rxBuf[reg.frame.dataLen] != expected) {
            logW("rx id=0x%02x: checksum 0x%02x != expected 0x%02x",
                 (unsigned)id, (unsigned)rxBuf[reg.frame.dataLen],
                 (unsigned)expected);
            continue;
          }
          reg.rxHandler(pid, rxBuf, reg.frame.dataLen);
        }
      }
    }

  }  // namespace lin
}  // namespace esp32m
