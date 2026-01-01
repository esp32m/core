#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "esp32m/app.hpp"

#include <rfc2217_server.h>

namespace esp32m {
  namespace net {
    namespace term {

      class RFC2217 : public AppObject {
       public:
        RFC2217(const RFC2217 &) = delete;
        RFC2217 &operator=(const RFC2217 &) = delete;

        static RFC2217 &instance();

        const char *name() const override {
          return "rfc2217";
        }

       private:
        RFC2217();

        void run();

        void *_server = nullptr;
        SemaphoreHandle_t _clientConnected = nullptr;
        SemaphoreHandle_t _clientDisconnected = nullptr;
        bool _sessionActive = false;

        void setupWatchdog();
        static void feedWatchdog();

        void handleClientConnected();
        void handleClientDisconnected();
        void handleDataReceived(const uint8_t *data, size_t len);
        unsigned handleBaudrate(unsigned baudrate);
        rfc2217_purge_t handlePurge(rfc2217_purge_t requested);

        static void onClientConnected(void *ctx);
        static void onClientDisconnected(void *ctx);
        static void onDataReceived(void *ctx, const uint8_t *data, size_t len);
        static unsigned onBaudrate(void *ctx, unsigned baudrate);
        static rfc2217_purge_t onPurge(void *ctx, rfc2217_purge_t requested);
      };

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
