#pragma once

#include "esp32m/app.hpp"
#include "esp32m/events.hpp"
#include "esp32m/io/pins.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace esp32m {
  namespace debug {
    
    class Button;

    namespace button {
      class Command : public Event {
       public:
        int command() const {
          return _command;
        }
        static bool is(Event &ev, int command) {
          return ev.is(NAME) && ((Command &)ev)._command == command;
        }

       private:
        Command(int command) : Event(NAME), _command(command) {}
        int _command;
        static const char *NAME;
        friend class debug::Button;
      };
    }  // namespace button

    class Button : public AppObject {
     public:
      Button(gpio_num_t pin);
      Button(const Button &) = delete;
      const char *name() const override {
        return "dbgbtn";
      }

     private:
      io::IPin *_pin;
      TaskHandle_t _task = nullptr;
      QueueHandle_t _queue = nullptr;
      unsigned long _stamp = 0;
      int _cmd = 0;
      void run();
    };

    Button *useButton(gpio_num_t pin = GPIO_NUM_0);
  }  // namespace debug
}  // namespace esp32m