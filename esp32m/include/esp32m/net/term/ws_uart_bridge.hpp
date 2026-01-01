#pragma once

#include <esp_err.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "esp32m/app.hpp"
#include "esp32m/net/term/uart.hpp"
#include "esp32m/ui/httpd.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      class WsUartBridge : public AppObject, public ui::IWSHandler {
       public:
        WsUartBridge(const WsUartBridge&) = delete;
        WsUartBridge& operator=(const WsUartBridge&) = delete;

        static WsUartBridge& instance();

        const char* name() const override {
          return "uart-ws";
        }

        // ui::IWSHandler
        esp_err_t handle(httpd_req_t* req) override;
        void sessionClosed(int sockfd) override;

       protected:
        bool handleRequest(Request& req) override;
        JsonDocument* getState(RequestContext& ctx) override;
        bool setConfig(RequestContext& ctx) override;
        JsonDocument* getConfig(RequestContext& ctx) override;

       private:
        WsUartBridge();

        esp_err_t beginSession(httpd_handle_t server, int fd);
        void endSession();

        bool isArmed() const;

        void startRxTask();
        static void rxTaskThunk(void* param);
        void rxLoop();

        esp_err_t sendBinaryAsync(const uint8_t* data, size_t len);
        void closeClientAsync();

        SemaphoreHandle_t _mutex = nullptr;

        httpd_handle_t _server = nullptr;
        int _fd = -1;
        TaskHandle_t _rxTask = nullptr;
        bool _sessionActive = false;

        uint32_t _armWindowMs = 5000;
        uint64_t _armedUntil = 0;

        UartConfig _uartCfg;
      };

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
