#pragma once

#include <driver/uart.h>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp32m/device.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/events/response.hpp"

namespace esp32m {

  namespace dev {
    class Uart : public Device {
     public:
      Uart(int num, uart_config_t &config);
      Uart(const Uart &) = delete;
      ~Uart();
      const char *name() const override {
        return _name;
      }

     protected:
      bool handleEvent(Event &ev) override;
      bool handleRequest(Request &req) override;

     private:
      int _num;
      TaskHandle_t _task = nullptr;
      char _name[6];
      QueueHandle_t _queue = nullptr;
      bool _stopped = false;
      std::mutex _mutex;
      Response *_pendingResponse = nullptr;
      void run();
    };

    void useUart(int num, uart_config_t &config);

  }  // namespace dev

}  // namespace esp32m