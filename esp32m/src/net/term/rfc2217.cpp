#include "esp32m/net/term/rfc2217.hpp"

#include <esp_task_wdt.h>

#include "esp32m/net/term/arbiter.hpp"
#include "esp32m/net/term/uart.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/net/net.hpp"

#include <freertos/task.h>

namespace esp32m {
  namespace net {
    namespace term {

      static constexpr unsigned kListenPort = 3333;
      static constexpr size_t kTaskStackSize = 4096;

      RFC2217 &RFC2217::instance() {
        static RFC2217 i;
        return i;
      }

      RFC2217::RFC2217() {
        xTaskCreate(
            [](void *param) {
              auto *self = static_cast<RFC2217 *>(param);
              self->run();
              vTaskDelete(nullptr);
            },
            "RFC2217", kTaskStackSize, this, 1, nullptr);
      }

      void RFC2217::setupWatchdog() {
        const esp_err_t addErr = esp_task_wdt_add(nullptr);
        if (addErr != ESP_OK && addErr != ESP_ERR_INVALID_STATE) {
          logW("esp_task_wdt_add failed: %s", esp_err_to_name(addErr));
        }
      }

      void RFC2217::feedWatchdog() {
        (void)esp_task_wdt_reset();
      }

      void RFC2217::handleClientConnected() {
        logI("RFC2217 client connected");
        if (_clientConnected)
          xSemaphoreGive(_clientConnected);
      }

      void RFC2217::handleClientDisconnected() {
        logI("RFC2217 client disconnected");
        if (_clientDisconnected)
          xSemaphoreGive(_clientDisconnected);
      }

      void RFC2217::handleDataReceived(const uint8_t *data, size_t len) {
        if (!_sessionActive || !len)
          return;
        (void)Uart::instance().write(data, len);
      }

      unsigned RFC2217::handleBaudrate(unsigned baudrate) {
        if (!_sessionActive || !baudrate)
          return 0;

        const esp_err_t err = Uart::instance().setBaudrate(baudrate);
        if (err != ESP_OK)
          return 0;

        logI("Baudrate set to %u", baudrate);
        return baudrate;
      }

      rfc2217_purge_t RFC2217::handlePurge(rfc2217_purge_t requested) {
        if (!_sessionActive)
          return (rfc2217_purge_t)-1;

        switch (requested) {
          case RFC2217_PURGE_RECEIVE:
            (void)Uart::instance().flushInput();
            return requested;
          case RFC2217_PURGE_TRANSMIT:
            (void)Uart::instance().waitTxDone(pdMS_TO_TICKS(1000));
            return requested;
          case RFC2217_PURGE_BOTH:
            (void)Uart::instance().flushInput();
            (void)Uart::instance().waitTxDone(pdMS_TO_TICKS(1000));
            return requested;
          default:
            return (rfc2217_purge_t)-1;
        }
      }

      void RFC2217::onClientConnected(void *ctx) {
        auto *self = static_cast<RFC2217 *>(ctx);
        if (self)
          self->handleClientConnected();
      }

      void RFC2217::onClientDisconnected(void *ctx) {
        auto *self = static_cast<RFC2217 *>(ctx);
        if (self)
          self->handleClientDisconnected();
      }

      void RFC2217::onDataReceived(void *ctx, const uint8_t *data, size_t len) {
        auto *self = static_cast<RFC2217 *>(ctx);
        if (self)
          self->handleDataReceived(data, len);
      }

      unsigned RFC2217::onBaudrate(void *ctx, unsigned baudrate) {
        auto *self = static_cast<RFC2217 *>(ctx);
        return self ? self->handleBaudrate(baudrate) : 0;
      }

      rfc2217_purge_t RFC2217::onPurge(void *ctx, rfc2217_purge_t requested) {
        auto *self = static_cast<RFC2217 *>(ctx);
        return self ? self->handlePurge(requested) : (rfc2217_purge_t)-1;
      }

      void RFC2217::run() {
        const esp_err_t uartErr = Uart::instance().ensure();
        if (uartErr != ESP_OK) {
          logE("UART init failed: %s", esp_err_to_name(uartErr));
          return;
        }

        const esp_err_t netifErr = net::useNetif();
        if (netifErr != ESP_OK) {
          logE("net::useNetif failed: %s", esp_err_to_name(netifErr));
          return;
        }

        const esp_err_t loopErr = net::useEventLoop();
        if (loopErr != ESP_OK) {
          logE("net::useEventLoop failed: %s", esp_err_to_name(loopErr));
          return;
        }

        setupWatchdog();

        _clientConnected = xSemaphoreCreateBinary();
        _clientDisconnected = xSemaphoreCreateBinary();
        if (!_clientConnected || !_clientDisconnected) {
          logE("Failed to create semaphores");
          return;
        }

        rfc2217_server_t server = nullptr;
        rfc2217_server_config_t cfg = {
            .ctx = this,
            .on_client_connected = onClientConnected,
            .on_client_disconnected = onClientDisconnected,
            .on_baudrate = onBaudrate,
            .on_control = nullptr,
          .on_purge = onPurge,
            .on_data_received = onDataReceived,
            .port = kListenPort,
            .task_stack_size = kTaskStackSize,
            .task_priority = 5,
            .task_core_id = 0,
        };

        const int createRes = rfc2217_server_create(&cfg, &server);
        if (createRes != 0 || !server) {
          logE("rfc2217_server_create failed: %d", createRes);
          return;
        }
        _server = server;

        logI("Starting RFC2217 server on port %u", cfg.port);
        const int startRes = rfc2217_server_start(server);
        if (startRes != 0) {
          logE("rfc2217_server_start failed: %d", startRes);
          rfc2217_server_destroy(server);
          _server = nullptr;
          return;
        }

        while (true) {
          logI("Waiting for RFC2217 client");

          while (xSemaphoreTake(_clientConnected, 0) == pdTRUE) {
          }
          while (xSemaphoreTake(_clientDisconnected, 0) == pdTRUE) {
          }

          while (xSemaphoreTake(_clientConnected, pdMS_TO_TICKS(1000)) != pdTRUE) {
            feedWatchdog();
          }

          const esp_err_t ownErr = Arbiter::instance().acquire(this, "rfc2217");
          if (ownErr != ESP_OK) {
            logW("UART is busy (owned by '%s'), ignoring session", Arbiter::instance().ownerName());
            while (xSemaphoreTake(_clientDisconnected, pdMS_TO_TICKS(1000)) != pdTRUE) {
              feedWatchdog();
            }
            continue;
          }

          _sessionActive = true;

          while (xSemaphoreTake(_clientDisconnected, 0) == pdTRUE) {
          }

          while (xSemaphoreTake(_clientDisconnected, 0) != pdTRUE) {
            static uint8_t buf[2048];
            const int len = Uart::instance().read(buf, sizeof(buf), pdMS_TO_TICKS(10));
            if (len > 0) {
              const int sendRes = rfc2217_server_send_data(server, buf, static_cast<size_t>(len));
              if (sendRes != 0) {
                logW("rfc2217_server_send_data failed: %d", sendRes);
                break;
              }
            }
            feedWatchdog();
          }

          _sessionActive = false;
          Arbiter::instance().release(this);
          feedWatchdog();
        }
      }

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
