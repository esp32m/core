#include "esp32m/net/term/ws_uart_bridge.hpp"

#include <cstring>

#include <esp_err.h>
#include <esp_idf_version.h>

#include "esp32m/base.hpp"
#include "esp32m/errors.hpp"
#include "esp32m/net/term/arbiter.hpp"
#include "esp32m/net/term/uart.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      static constexpr const char *kWsUri = "/ws/uart";
      static constexpr size_t kMaxFrame = 4096;
      static constexpr size_t kRxChunk = 512;

      WsUartBridge &WsUartBridge::instance() {
        static WsUartBridge i;
        return i;
      }

      WsUartBridge::WsUartBridge() : ui::IWSHandler(kWsUri) {
        _mutex = xSemaphoreCreateMutex();
        _uartCfg = Uart::instance().defaults();
        auto *httpd = ui::Httpd::instance();
        if (httpd)
          (void)httpd->addWsHandler(*this);
      }

      bool WsUartBridge::isArmed() const {
        const auto now = millis();
        return _armedUntil && now < _armedUntil;
      }

      bool WsUartBridge::handleRequest(Request &req) {
        if (AppObject::handleRequest(req))
          return true;

        if (req.is("arm")) {
          _armedUntil = millis() + _armWindowMs;
          req.respond();
          return true;
        }

        if (req.is("disarm")) {
          _armedUntil = 0;
          req.respond();
          return true;
        }

        return false;
      }

      JsonDocument *WsUartBridge::getState(RequestContext &ctx) {
        auto doc = new JsonDocument();
        auto root = doc->to<JsonObject>();

        root["armed"] = isArmed();
        root["active"] = _sessionActive;
        root["uartOwner"] = Arbiter::instance().ownerName();

        return doc;
      }

      JsonDocument *WsUartBridge::getConfig(RequestContext &ctx) {
        auto doc = new JsonDocument();
        auto root = doc->to<JsonObject>();

        root["port"] = static_cast<int>(_uartCfg.port);
        root["txGpio"] = _uartCfg.txGpio;
        root["rxGpio"] = _uartCfg.rxGpio;
        root["baudrate"] = _uartCfg.baudrate;
        root["dataBits"] = static_cast<int>(_uartCfg.dataBits);
        root["parity"] = static_cast<int>(_uartCfg.parity);
        root["stopBits"] = static_cast<int>(_uartCfg.stopBits);
        root["flowCtrl"] = static_cast<int>(_uartCfg.flowCtrl);
        root["rxFlowCtrlThresh"] = _uartCfg.rxFlowCtrlThresh;
        root["sourceClk"] = static_cast<int>(_uartCfg.sourceClk);

        (void)ctx;
        return doc;
      }

      bool WsUartBridge::setConfig(RequestContext &ctx) {
        if (_sessionActive) {
          ctx.errors.add(ErrBusy);
          return false;
        }

        bool changed = false;
        auto ca = ctx.data.as<JsonObjectConst>();

        json::from(ca["baudrate"], _uartCfg.baudrate, &changed);
        json::fromIntCastable(ca["dataBits"], _uartCfg.dataBits, &changed);
        json::fromIntCastable(ca["parity"], _uartCfg.parity, &changed);
        json::fromIntCastable(ca["stopBits"], _uartCfg.stopBits, &changed);
        json::fromIntCastable(ca["flowCtrl"], _uartCfg.flowCtrl, &changed);
        json::from(ca["rxFlowCtrlThresh"], _uartCfg.rxFlowCtrlThresh, &changed);
        json::fromIntCastable(ca["sourceClk"], _uartCfg.sourceClk, &changed);

        // Keep port/pins fixed to the UART defaults unless explicitly provided.
        json::fromIntCastable(ca["port"], _uartCfg.port, &changed);
        json::from(ca["txGpio"], _uartCfg.txGpio, &changed);
        json::from(ca["rxGpio"], _uartCfg.rxGpio, &changed);

        if (_uartCfg.baudrate == 0) {
          _uartCfg.baudrate = 115200;
          changed = true;
        }

        return changed;
      }

      esp_err_t WsUartBridge::beginSession(httpd_handle_t server, int fd) {
        if (!_mutex)
          return ESP_ERR_NO_MEM;

        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
          return ESP_FAIL;

        esp_err_t res = ESP_OK;
        if (_sessionActive) {
          res = ESP_ERR_INVALID_STATE;
        } else {
          const esp_err_t ownErr = Arbiter::instance().acquire(this, "ws-uart");
          if (ownErr != ESP_OK) {
            res = ownErr;
          } else {
            const esp_err_t uartErr = Uart::instance().apply(_uartCfg);
            if (uartErr != ESP_OK) {
              Arbiter::instance().release(this);
              res = uartErr;
            } else {
            _server = server;
            _fd = fd;
            _sessionActive = true;
            startRxTask();
            }
          }
        }

        xSemaphoreGive(_mutex);
        return res;
      }

      void WsUartBridge::endSession() {
        if (!_mutex)
          return;

        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
          return;

        const bool wasActive = _sessionActive;
        _sessionActive = false;
        _server = nullptr;
        _fd = -1;
        _rxTask = nullptr;

        xSemaphoreGive(_mutex);

        if (wasActive) {
          Arbiter::instance().release(this);
        }
      }

      void WsUartBridge::startRxTask() {
        if (_rxTask)
          return;
        xTaskCreate(rxTaskThunk, "ws-uart-rx", 4096, this, 1, &_rxTask);
      }

      void WsUartBridge::rxTaskThunk(void *param) {
        auto *self = static_cast<WsUartBridge *>(param);
        if (self)
          self->rxLoop();
        vTaskDelete(nullptr);
      }

      esp_err_t WsUartBridge::sendBinaryAsync(const uint8_t *data, size_t len) {
        if (!_server || _fd < 0 || !data || !len)
          return ESP_ERR_INVALID_STATE;

        struct WorkItem {
          httpd_handle_t server;
          int fd;
          uint8_t *payload;
          size_t len;
        };

        auto *work = (WorkItem *)calloc(1, sizeof(WorkItem));
        if (!work)
          return ESP_ERR_NO_MEM;

        auto *payload = (uint8_t *)malloc(len);
        if (!payload) {
          free(work);
          return ESP_ERR_NO_MEM;
        }
        memcpy(payload, data, len);

        work->server = _server;
        work->fd = _fd;
        work->payload = payload;
        work->len = len;

        auto fn = [](void *arg) {
          auto *w = (WorkItem *)arg;
          httpd_ws_frame_t ws_pkt;
          memset(&ws_pkt, 0, sizeof(ws_pkt));
          ws_pkt.payload = w->payload;
          ws_pkt.len = w->len;
          ws_pkt.type = HTTPD_WS_TYPE_BINARY;
          (void)httpd_ws_send_frame_async(w->server, w->fd, &ws_pkt);
          free(w->payload);
          free(w);
        };

        const esp_err_t err = httpd_queue_work(_server, fn, work);
        if (err != ESP_OK) {
          free(payload);
          free(work);
        }
        return err;
      }

      void WsUartBridge::closeClientAsync() {
        if (!_server || _fd < 0)
          return;

        struct WorkItem {
          httpd_handle_t server;
          int fd;
        };

        auto *work = (WorkItem *)calloc(1, sizeof(WorkItem));
        if (!work)
          return;
        work->server = _server;
        work->fd = _fd;

        auto fn = [](void *arg) {
          auto *w = (WorkItem *)arg;
          httpd_ws_frame_t ws_pkt;
          memset(&ws_pkt, 0, sizeof(ws_pkt));
          ws_pkt.type = HTTPD_WS_TYPE_CLOSE;
          ws_pkt.payload = nullptr;
          ws_pkt.len = 0;
          (void)httpd_ws_send_frame_async(w->server, w->fd, &ws_pkt);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
          (void)httpd_sess_trigger_close(w->server, w->fd);
#endif
          free(w);
        };

        (void)httpd_queue_work(_server, fn, work);
      }

      void WsUartBridge::rxLoop() {
        logI("WS UART RX task started");

        while (true) {
          bool active = false;
          if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            active = _sessionActive;
            xSemaphoreGive(_mutex);
          }
          if (!active)
            break;

          uint8_t buf[kRxChunk];
          const int n = Uart::instance().read(buf, sizeof(buf), pdMS_TO_TICKS(10));
          if (n > 0) {
            const esp_err_t err = sendBinaryAsync(buf, (size_t)n);
            if (err != ESP_OK) {
              logW("sendBinaryAsync failed: %s", esp_err_to_name(err));
              break;
            }
          }
        }

        logI("WS UART RX task ending");
        endSession();
      }

      esp_err_t WsUartBridge::handle(httpd_req_t *req) {
        if (!req)
          return ESP_ERR_INVALID_ARG;

        const int fd = httpd_req_to_sockfd(req);

        // Handshake stage: accept only if armed and UART is free.
        if (req->method == HTTP_GET) {
          if (!isArmed()) {
            logW("WS UART handshake refused: not armed");
            httpd_resp_set_status(req, "403 Forbidden");
            (void)httpd_resp_send(req, nullptr, 0);
            return ESP_FAIL;
          }

          const esp_err_t ownErr = beginSession(req->handle, fd);
          if (ownErr != ESP_OK) {
            logW("WS UART handshake refused: %s", esp_err_to_name(ownErr));
            httpd_resp_set_status(req, "409 Conflict");
            (void)httpd_resp_send(req, nullptr, 0);
            return ESP_FAIL;
          }

          logI("WS UART session started");
          _armedUntil = 0;  // one-shot arm
          return ESP_OK;
        }

        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(ws_pkt));
        const esp_err_t hdrErr = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if (hdrErr != ESP_OK) {
          logW("ws recv header failed: %s", esp_err_to_name(hdrErr));
          return hdrErr;
        }

        if (ws_pkt.len > kMaxFrame) {
          logW("WS frame too large: %u", (unsigned)ws_pkt.len);
          closeClientAsync();
          return ESP_OK;
        }

        uint8_t *payload = nullptr;
        if (ws_pkt.len) {
          payload = (uint8_t *)calloc(1, ws_pkt.len);
          if (!payload)
            return ESP_ERR_NO_MEM;
          ws_pkt.payload = payload;
        }

        const esp_err_t recvErr = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (recvErr != ESP_OK) {
          if (payload)
            free(payload);
          return recvErr;
        }

        if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
          if (payload)
            free(payload);
          endSession();
          return ESP_OK;
        }

        if (ws_pkt.type != HTTPD_WS_TYPE_BINARY && ws_pkt.type != HTTPD_WS_TYPE_TEXT) {
          if (payload)
            free(payload);
          return ESP_OK;
        }

        if (!_sessionActive) {
          logW("WS UART data received without active session");
          if (payload)
            free(payload);
          closeClientAsync();
          return ESP_OK;
        }

        if (ws_pkt.len && payload) {
          (void)Uart::instance().write(payload, ws_pkt.len);
        }

        if (payload)
          free(payload);

        return ESP_OK;
      }

      void WsUartBridge::sessionClosed(int sockfd) {
        if (sockfd >= 0 && sockfd == _fd)
          endSession();
      }

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
