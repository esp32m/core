#pragma once

#include <cstddef>
#include <cstdint>

#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <rfc2217_server.h>

#include "esp32m/logging.hpp"
#include "esp32m/net/term/uart.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      struct Rfc2217UartServerConfig {
        Rfc2217UartServerConfig() = default;

        uint16_t tcpPort = 3333;

        // Optional: apply this UART config when a session starts.
        bool applyUartConfig = false;
        UartConfig uartConfig;

        // Task/server settings
        size_t taskStackSize = 4096;
        unsigned taskPriority = 5;
        unsigned taskCoreId = 0;

        // Arbiter owner name used while a client is connected.
        const char *ownerName = "rfc2217";

        // Optional session lifecycle hooks.
        void *hooksCtx = nullptr;
        void (*onSessionStarted)(void *ctx) = nullptr;
        void (*onSessionEnded)(void *ctx) = nullptr;

        // Optional extra hooks in addition to the built-in UART forwarding.
        void (*onClientConnected)(void *ctx) = nullptr;
        void (*onClientDisconnected)(void *ctx) = nullptr;

        // Optional: handle baudrate changes. If nullptr, requested baudrate is
        // applied directly to UART.
        void *baudrateCtx = nullptr;
        rfc2217_on_baudrate_t onBaudrate = nullptr;

        // If true, the server always reports the *requested* baudrate back to
        // the RFC2217 client (even if the physical UART baudrate is kept
        // unchanged or clamped). Some clients (pyserial) treat differing
        // responses as a hard error.
        bool reportRequestedBaudrate = false;

        // Optional: handle control lines (DTR/RTS/etc). If nullptr, control
        // requests are accepted but ignored.
        void *controlCtx = nullptr;
        rfc2217_on_control_t onControl = nullptr;
      };

      class Rfc2217UartServer : public log::Loggable {
       public:
        Rfc2217UartServer(const Rfc2217UartServer &) = delete;
        Rfc2217UartServer &operator=(const Rfc2217UartServer &) = delete;

        explicit Rfc2217UartServer(const Rfc2217UartServerConfig &cfg);
        ~Rfc2217UartServer();

        const char *name() const override {
          return "rfc2217-uart";
        }

        void start();

        bool sessionActive() const {
          return _sessionActive;
        }

       private:
        void run();

        void setupWatchdog();
        static void feedWatchdog();

        void handleClientConnected();
        void handleClientDisconnected();
        void handleDataReceived(const uint8_t *data, size_t len);
        unsigned handleBaudrate(unsigned baudrate);
        rfc2217_control_t handleControl(rfc2217_control_t requested);
        rfc2217_purge_t handlePurge(rfc2217_purge_t requested);

        static void onClientConnectedThunk(void *ctx);
        static void onClientDisconnectedThunk(void *ctx);
        static void onDataReceivedThunk(void *ctx, const uint8_t *data, size_t len);
        static unsigned onBaudrateThunk(void *ctx, unsigned baudrate);
        static rfc2217_control_t onControlThunk(void *ctx, rfc2217_control_t requested);
        static rfc2217_purge_t onPurgeThunk(void *ctx, rfc2217_purge_t requested);

        Rfc2217UartServerConfig _cfg;

        rfc2217_server_t _server = nullptr;
        SemaphoreHandle_t _clientConnected = nullptr;
        SemaphoreHandle_t _clientDisconnected = nullptr;

        bool _sessionActive = false;

        // RFC2217 clients (pyserial) can send baudrate/purge options immediately
        // upon TCP connect, before the UART is acquired via Arbiter.
        std::atomic<unsigned> _pendingBaudrate{0};
      };

    }  // namespace term
  }  // namespace net
}  // namespace esp32m
