#include "esp32m/net/term/monitor.hpp"

#include <algorithm>
#include <cstring>

#include "esp32m/base.hpp"
#include "esp32m/net/term/arbiter.hpp"
#include "esp32m/net/term/uart.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      struct LineItem {
        size_t len;
        char data[1];
      };

      class Monitor::RxTap : public IRxTap {
       public:
        explicit RxTap(Monitor* m) : _m(m) {}

        void onRx(const uint8_t* data, size_t len) override {
          if (_m)
            _m->ingest(data, len);
        }

       private:
        Monitor* _m;
      };

      Monitor& Monitor::instance() {
        static Monitor i;
        return i;
      }

      Monitor::Monitor() {
        _queue = xQueueCreate(64, sizeof(LineItem*));
        if (!_queue) {
          logE("failed to create queue");
          return;
        }

        _tap = new RxTap(this);
        Uart::instance().setRxTap(_tap);

        xTaskCreate(rxTaskThunk, "term-mon-rx", 4096, this, 1, &_rxTask);
        xTaskCreate(dispatchTaskThunk, "term-mon-disp", 4096, this, 1,
                    &_dispatchTask);
      }

      void Monitor::add(IConsumer& consumer) {
        std::lock_guard<std::mutex> guard(_consumersMutex);
        if (std::find(_consumers.begin(), _consumers.end(), &consumer) ==
            _consumers.end()) {
          _consumers.push_back(&consumer);
        }
      }

      void Monitor::remove(IConsumer& consumer) {
        std::lock_guard<std::mutex> guard(_consumersMutex);
        auto it = std::remove(_consumers.begin(), _consumers.end(), &consumer);
        _consumers.erase(it, _consumers.end());
      }

      void Monitor::enqueueLine(const char* data, size_t len) {
        if (!_queue)
          return;

        const size_t allocSize = sizeof(LineItem) + len + 1;
        auto* item = (LineItem*)malloc(allocSize);
        if (!item)
          return;

        item->len = len;
        if (len)
          memcpy(item->data, data, len);
        item->data[len] = 0;

        if (xQueueSend(_queue, &item, 0) != pdTRUE) {
          free(item);
        }
      }

      void Monitor::ingest(const uint8_t* data, size_t len) {
        if (!data || !len)
          return;

        std::vector<std::string> ready;
        ready.reserve(4);

        {
          auto flush = [&]() {
            // Preserve empty lines (can be meaningful in logs).
            ready.emplace_back(std::move(_buf));
            _buf.clear();
          };

          std::lock_guard<std::mutex> guard(_bufMutex);

          for (size_t i = 0; i < len; i++) {
            const char c = (char)data[i];

            if (c == '\n') {
              if (_lastWasCr) {
                _lastWasCr = false;
                continue;
              }
              flush();
              continue;
            }

            if (c == '\r') {
              flush();
              _lastWasCr = true;
              continue;
            }

            _lastWasCr = false;

            if (_buf.size() >= _maxLineLen) {
              flush();
            }

            _buf.push_back(c);
          }
        }

        for (auto& line : ready) {
          enqueueLine(line.data(), line.size());
        }
      }

      void Monitor::rxTaskThunk(void* param) {
        auto* self = static_cast<Monitor*>(param);
        if (self)
          self->rxLoop();
        vTaskDelete(nullptr);
      }

      void Monitor::rxLoop() {
        if (Uart::instance().ensure() != ESP_OK) {
          logE("uart ensure failed");
          return;
        }

        static uint8_t buf[256];

        while (true) {
          if (Arbiter::instance().ownerName()[0]) {
            vTaskDelay(pdMS_TO_TICKS(_idlePollMs));
            continue;
          }

          // Read only when idle (no active owner). When a session is active,
          // it reads via Uart::read() and our RX tap sees the bytes.
          (void)Uart::instance().read(buf, sizeof(buf),
                                      pdMS_TO_TICKS(_idlePollMs));
        }
      }

      void Monitor::dispatchTaskThunk(void* param) {
        auto* self = static_cast<Monitor*>(param);
        if (self)
          self->dispatchLoop();
        vTaskDelete(nullptr);
      }

      void Monitor::dispatchLoop() {
        while (true) {
          LineItem* item = nullptr;
          if (!_queue ||
              xQueueReceive(_queue, &item, portMAX_DELAY) != pdTRUE || !item)
            continue;

          std::string line(item->data, item->len);
          free(item);
          

          std::vector<IConsumer*> consumers;
          {
            std::lock_guard<std::mutex> guard(_consumersMutex);
            consumers = _consumers;
          }

          for (size_t i = 0; i < consumers.size(); i++) {
            auto* c = consumers[i];
            if (!c)
              continue;
            if (i + 1 == consumers.size())
              c->consume(std::move(line));
            else
              c->consume(line);
          }
        }
      }

    }  // namespace term
  }  // namespace net
}  // namespace esp32m
