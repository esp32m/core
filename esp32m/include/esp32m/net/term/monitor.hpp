#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      class IConsumer {
       public:
        virtual ~IConsumer() = default;
        virtual void consume(std::string line) = 0;
      };

      class Monitor : public log::Loggable {
       public:
        Monitor(const Monitor&) = delete;
        Monitor& operator=(const Monitor&) = delete;

        static Monitor& instance();

        const char* name() const override {
          return "term-monitor";
        }

        void add(IConsumer& consumer);
        void remove(IConsumer& consumer);

       protected:
        Monitor();

       private:
        class RxTap;
        void ingest(const uint8_t *data, size_t len);

        static void rxTaskThunk(void *param);
        void rxLoop();

        static void dispatchTaskThunk(void *param);
        void dispatchLoop();

        void enqueueLine(const char *data, size_t len);

        std::vector<IConsumer *> _consumers;
        std::mutex _consumersMutex;

        std::string _buf;
        std::mutex _bufMutex;
        bool _lastWasCr = false;

        RxTap *_tap = nullptr;

        QueueHandle_t _queue = nullptr;
        TaskHandle_t _rxTask = nullptr;
        TaskHandle_t _dispatchTask = nullptr;

        size_t _maxLineLen = 1024;
        uint32_t _idlePollMs = 25;
      };
    }  // namespace term
  }  // namespace net
}  // namespace esp32m