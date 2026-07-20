#pragma once

#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <functional>
#include <map>
#include <mutex>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace lin {

    /// Maximum payload bytes in one LIN frame.
    static constexpr uint8_t MaxDataLen = 8;

    /// Sync field value defined by the LIN specification.
    static constexpr uint8_t SyncByte = 0x55;

    /// Checksum type.  Choose Classic for LIN 1.x slaves, Enhanced for LIN 2.x.
    /// Diagnostic frames (ID 0x3C / 0x3D) always use Classic regardless of the
    /// node's LIN version.
    enum class Checksum {
      Classic,   ///< LIN 1.x — sum of data bytes only.
      Enhanced,  ///< LIN 2.x — sum of PID + data bytes.
    };

    /// Descriptor for a single LIN frame.
    struct Frame {
      uint8_t id;                           ///< 6-bit frame ID (0x00–0x3F).
      uint8_t dataLen;                      ///< Payload length in bytes (1–8).
      Checksum checksum = Checksum::Enhanced;
    };

    /// Compute the Protected Identifier byte (6-bit ID + 2 parity bits P0/P1).
    uint8_t computePid(uint8_t id);

    /// Compute the LIN checksum over @p data.
    /// For Checksum::Enhanced the PID is included in the running sum.
    uint8_t computeChecksum(uint8_t pid, const uint8_t *data, uint8_t len,
                            Checksum cs);

    // -----------------------------------------------------------------------
    /// Common base: owns the UART peripheral and provides break generation.
    ///
    /// Neither Master nor Slave should be used after construction if
    /// the constructor logged an error — the UART driver will not have been
    /// installed and all operations will fail gracefully.
    // -----------------------------------------------------------------------
    class Lin : public log::Loggable {
     public:
      Lin(uart_port_t port, int txPin, int rxPin, uint32_t baud = 19200);
      virtual ~Lin();

      uart_port_t port() const { return _port; }
      uint32_t baud() const { return _baud; }

     protected:
      uart_port_t _port;
      int _txPin, _rxPin;
      uint32_t _baud;
      bool _driverInstalled = false;
      std::mutex _mutex;

      /// Send a LIN break field using the baud-rate-halving method:
      /// sets TX baud to baud/2, transmits 0x00 (= 20 dominant bit-times at
      /// full baud, satisfying the ≥13 bit LIN requirement), then restores
      /// the original baud rate.
      esp_err_t sendBreak();

      /// Flush the UART RX ring buffer.
      void flushRx();

      /// Delete the UART driver.  Safe to call more than once (idempotent).
      void destroyDriver();
    };

    // -----------------------------------------------------------------------
    /// LIN master: initiates all bus transactions.
    ///
    /// The caller is responsible for scheduling when publish() / request()
    /// are called; this class does not contain a built-in frame scheduler.
    ///
    /// All public methods are thread-safe and may be called concurrently
    /// from multiple tasks.
    // -----------------------------------------------------------------------
    class Master : public Lin {
     public:
      /// Invoked from the calling task context when a slave response has been
      /// received and its checksum verified.
      ///
      /// @param pid     Protected Identifier byte sent in the header.
      /// @param data    Received payload (frame.dataLen bytes).
      /// @param len     Number of data bytes.
      using RxCallback =
          std::function<void(uint8_t pid, const uint8_t *data, uint8_t len)>;

      /// @param port         UART port to use.
      /// @param txPin        GPIO number wired to TJA1021 TXD.
      /// @param rxPin        GPIO number wired to TJA1021 RXD.
      /// @param baud         LIN baud rate (typical: 2400 / 9600 / 19200).
      /// @param rxCallback   Optional; required for request() to work.
      Master(uart_port_t port, int txPin, int rxPin, uint32_t baud = 19200,
             RxCallback rxCallback = nullptr);

      const char *name() const override { return "lin_master"; }

      /// Publish frame: master transmits header + data + checksum.
      /// No slave response is expected.
      ///
      /// @param frame      Frame descriptor (ID, length, checksum type).
      /// @param data       Payload to send (must be frame.dataLen bytes).
      /// @param timeoutMs  TX completion timeout in milliseconds.
      esp_err_t publish(const Frame &frame, const uint8_t *data,
                        int timeoutMs = 100);

      /// Request frame: master transmits the header only and waits for the
      /// addressed slave to supply the response.  The verified payload is
      /// forwarded to the RxCallback provided at construction time.
      ///
      /// Returns ESP_ERR_INVALID_STATE if no RxCallback was supplied.
      /// Returns ESP_ERR_TIMEOUT      if the slave did not respond in time.
      /// Returns ESP_ERR_INVALID_CRC  if the received checksum is wrong.
      ///
      /// @param frame      Frame descriptor (ID, length, checksum type).
      /// @param timeoutMs  Response wait timeout in milliseconds.
      esp_err_t request(const Frame &frame, int timeoutMs = 100);

     private:
      RxCallback _rxCallback;

      /// Transmit break + sync + PID and flush their UART echo bytes.
      esp_err_t sendHeader(uint8_t pid);
    };

    // -----------------------------------------------------------------------
    /// LIN slave: listens for master-initiated frames and responds or
    /// consumes data according to per-ID handler registrations.
    ///
    /// A background task monitors the bus for break fields via the UART
    /// event queue and dispatches each received header to the appropriate
    /// registered handler.
    // -----------------------------------------------------------------------
    class Slave : public Lin {
     public:
      /// Called when the master issues a request header for this frame ID.
      /// The handler must fill exactly @p dataLen bytes into @p data and
      /// return true to transmit a response, or return false to stay silent.
      using TxHandler =
          std::function<bool(uint8_t pid, uint8_t *data, uint8_t dataLen)>;

      /// Called when the master publishes data for this frame ID and the
      /// checksum has been verified.
      using RxHandler =
          std::function<void(uint8_t pid, const uint8_t *data, uint8_t dataLen)>;

      /// @param port    UART port to use.
      /// @param txPin   GPIO number wired to TJA1021 TXD.
      /// @param rxPin   GPIO number wired to TJA1021 RXD.
      /// @param baud    LIN baud rate (must match the master).
      Slave(uart_port_t port, int txPin, int rxPin, uint32_t baud = 19200);
      ~Slave() override;

      const char *name() const override { return "lin_slave"; }

      /// Register a handler that supplies response data when the master
      /// requests this frame ID.  Replaces any previously registered handler
      /// for the same ID.
      void setTxHandler(const Frame &frame, TxHandler handler);

      /// Register a handler that consumes data published by the master for
      /// this frame ID.  Replaces any previously registered handler for the
      /// same ID.
      void setRxHandler(const Frame &frame, RxHandler handler);

     private:
      struct Registration {
        Frame frame;
        TxHandler txHandler;
        RxHandler rxHandler;
      };

      std::map<uint8_t, Registration> _registrations;  // keyed by 6-bit ID
      QueueHandle_t _uartEventQueue = nullptr;
      TaskHandle_t _task = nullptr;

      void run();
    };

  }  // namespace lin
}  // namespace esp32m
