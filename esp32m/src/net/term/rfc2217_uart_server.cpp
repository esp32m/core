#include "esp32m/net/term/rfc2217_uart_server.hpp"

#include <esp_task_wdt.h>

#include "esp32m/base.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/net/net.hpp"
#include "esp32m/net/term/arbiter.hpp"
#include "esp32m/net/term/uart.hpp"

#include <freertos/task.h>

namespace esp32m {
  namespace net {
    namespace term {

      Rfc2217UartServer::Rfc2217UartServer(const Rfc2217UartServerConfig &cfg)
          : _cfg(cfg) {
        if (!_cfg.tcpPort)
          _cfg.tcpPort = 3333;
        if (!_cfg.ownerName)
          _cfg.ownerName = "rfc2217";

        // Stack size/priority occasionally end up uninitialized when config is
        // default-constructed without braces in user code. Clamp to sane ranges.
        if (_cfg.taskStackSize < 2048 || _cfg.taskStackSize > 32768)
          _cfg.taskStackSize = 4096;
        if (_cfg.taskPriority < 1 || _cfg.taskPriority > 24)
          _cfg.taskPriority = 5;

        if (!_cfg.uartConfig.baudrate)
          _cfg.uartConfig.baudrate = 115200;
      }

      Rfc2217UartServer::~Rfc2217UartServer() {
        // Best-effort cleanup. In typical usage this object lives forever.
        if (_server) {
          (void)rfc2217_server_stop(_server);
          rfc2217_server_destroy(_server);
          _server = nullptr;
        }
        if (_clientConnected) {
          vSemaphoreDelete(_clientConnected);
          _clientConnected = nullptr;
        }
        if (_clientDisconnected) {
          vSemaphoreDelete(_clientDisconnected);
          _clientDisconnected = nullptr;
        }
      }

      void Rfc2217UartServer::start() {
        xTaskCreate(
            [](void *param) {
              auto *self = static_cast<Rfc2217UartServer *>(param);
              if (self)
                self->run();
              vTaskDelete(nullptr);
            },
            makeTaskName(_cfg.ownerName), _cfg.taskStackSize, this, _cfg.taskPriority,
            nullptr);
      }

      void Rfc2217UartServer::setupWatchdog() {
        const esp_err_t addErr = esp_task_wdt_add(nullptr);
        if (addErr != ESP_OK && addErr != ESP_ERR_INVALID_STATE) {
          logW("esp_task_wdt_add failed: %s", esp_err_to_name(addErr));
        }
      }

      void Rfc2217UartServer::feedWatchdog() {
        (void)esp_task_wdt_reset();
      }

      void Rfc2217UartServer::handleClientConnected() {
        logI("RFC2217 client connected");
        if (_cfg.onClientConnected)
          _cfg.onClientConnected(_cfg.hooksCtx);
        if (_clientConnected)
          xSemaphoreGive(_clientConnected);
      }

      void Rfc2217UartServer::handleClientDisconnected() {
        logI("RFC2217 client disconnected");
        if (_cfg.onClientDisconnected)
          _cfg.onClientDisconnected(_cfg.hooksCtx);
        if (_clientDisconnected)
          xSemaphoreGive(_clientDisconnected);
      }

      void Rfc2217UartServer::handleDataReceived(const uint8_t *data, size_t len) {
        if (!_sessionActive || !len)
          return;
        (void)Uart::instance().write(data, len);
      }

      unsigned Rfc2217UartServer::handleBaudrate(unsigned baudrate) {
        if (!baudrate)
          return 0;

        unsigned accepted = baudrate;
        if (_cfg.onBaudrate) {
          accepted = _cfg.onBaudrate(_cfg.baudrateCtx, baudrate);
          if (!accepted)
            return 0;
        }

        if (!_sessionActive) {
          // Client can negotiate baudrate before we own the UART.
          _pendingBaudrate.store(accepted, std::memory_order_relaxed);
          return _cfg.reportRequestedBaudrate ? baudrate : accepted;
        }

        const esp_err_t err = Uart::instance().setBaudrate(accepted);
        if (err != ESP_OK)
          return 0;

        if (_cfg.reportRequestedBaudrate && accepted != baudrate) {
          logI("Baudrate requested %u, applied %u", baudrate, accepted);
          return baudrate;
        }

        logI("Baudrate set to %u", accepted);
        return accepted;
      }

      rfc2217_control_t Rfc2217UartServer::handleControl(rfc2217_control_t requested) {
        if (_cfg.onControl)
          return _cfg.onControl(_cfg.controlCtx, requested);
        // No control support: accept request but ignore.
        return requested;
      }

      rfc2217_purge_t Rfc2217UartServer::handlePurge(rfc2217_purge_t requested) {
        // Always ACK purge requests immediately.
        // pyserial issues PURGE during open(); blocking here can stall RFC2217
        // negotiation and cause timeouts.
        return requested;
      }

      void Rfc2217UartServer::onClientConnectedThunk(void *ctx) {
        auto *self = static_cast<Rfc2217UartServer *>(ctx);
        if (self)
          self->handleClientConnected();
      }

      void Rfc2217UartServer::onClientDisconnectedThunk(void *ctx) {
        auto *self = static_cast<Rfc2217UartServer *>(ctx);
        if (self)
          self->handleClientDisconnected();
      }

      void Rfc2217UartServer::onDataReceivedThunk(void *ctx, const uint8_t *data,
                                                  size_t len) {
        auto *self = static_cast<Rfc2217UartServer *>(ctx);
        if (self)
          self->handleDataReceived(data, len);
      }

      unsigned Rfc2217UartServer::onBaudrateThunk(void *ctx, unsigned baudrate) {
        auto *self = static_cast<Rfc2217UartServer *>(ctx);
        return self ? self->handleBaudrate(baudrate) : 0;
      }

      rfc2217_control_t Rfc2217UartServer::onControlThunk(void *ctx,
                                                         rfc2217_control_t requested) {
        auto *self = static_cast<Rfc2217UartServer *>(ctx);
        return self ? self->handleControl(requested) : (rfc2217_control_t)-1;
      }

      rfc2217_purge_t Rfc2217UartServer::onPurgeThunk(void *ctx,
                                                     rfc2217_purge_t requested) {
        auto *self = static_cast<Rfc2217UartServer *>(ctx);
        return self ? self->handlePurge(requested) : (rfc2217_purge_t)-1;
      }

      void Rfc2217UartServer::run() {
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
            .on_client_connected = onClientConnectedThunk,
            .on_client_disconnected = onClientDisconnectedThunk,
            .on_baudrate = onBaudrateThunk,
            .on_control = onControlThunk,
            .on_purge = onPurgeThunk,
            .on_data_received = onDataReceivedThunk,
            .port = _cfg.tcpPort,
            .task_stack_size = _cfg.taskStackSize,
            .task_priority = _cfg.taskPriority,
            .task_core_id = _cfg.taskCoreId,
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

          bool acquired = false;
          bool warnedBusy = false;
          while (!acquired) {
            const esp_err_t ownErr = Arbiter::instance().acquire(this, _cfg.ownerName);
            if (ownErr == ESP_OK) {
              acquired = true;
              break;
            }

            if (!warnedBusy) {
              logW("UART is busy (owned by '%s'), waiting...", Arbiter::instance().ownerName());
              warnedBusy = true;
            }

            // If the client disconnects while we're waiting, abandon this session.
            if (xSemaphoreTake(_clientDisconnected, pdMS_TO_TICKS(100)) == pdTRUE)
              break;
            feedWatchdog();
          }

          if (!acquired)
            continue;

          if (_cfg.applyUartConfig) {
            const esp_err_t applyErr = Uart::instance().apply(_cfg.uartConfig);
            if (applyErr != ESP_OK) {
              logW("Uart::apply failed: %s", esp_err_to_name(applyErr));
              Arbiter::instance().release(this);
              while (xSemaphoreTake(_clientDisconnected, pdMS_TO_TICKS(1000)) != pdTRUE) {
                feedWatchdog();
              }
              continue;
            }
          }

          const unsigned pendingBaudrate =
              _pendingBaudrate.exchange(0, std::memory_order_relaxed);
          if (pendingBaudrate) {
            const esp_err_t brErr = Uart::instance().setBaudrate(pendingBaudrate);
            if (brErr != ESP_OK)
              logW("setBaudrate(%u) failed: %s", pendingBaudrate,
                   esp_err_to_name(brErr));
          }

          // Call the hook before enabling TCP->UART forwarding. This allows
          // session-start sequences (e.g. reset-to-bootloader pulses) to run
          // without incoming client bytes being forwarded to the target.
          if (_cfg.onSessionStarted)
            _cfg.onSessionStarted(_cfg.hooksCtx);
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

          if (_cfg.onSessionEnded)
            _cfg.onSessionEnded(_cfg.hooksCtx);
          _sessionActive = false;
          Arbiter::instance().release(this);
          feedWatchdog();
        }
      }

    }  // namespace term
  }  // namespace net
}  // namespace esp32m
