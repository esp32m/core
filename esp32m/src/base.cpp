#include "esp32m/base.hpp"

#include <esp_attr.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <map>

#define NOP() asm volatile("nop")

namespace esp32m {

#ifndef ARDUINO
  unsigned long IRAM_ATTR micros() {
    return (unsigned long)(esp_timer_get_time());
  }

  unsigned long IRAM_ATTR millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
  }

  void delay(uint32_t ms) {
    vTaskDelay(ms / portTICK_PERIOD_MS);
  }

  void IRAM_ATTR delayMicroseconds(uint32_t us) {
    uint32_t m = micros();
    if (us) {
      uint32_t e = (m + us);
      if (m > e) {  // overflow
        while (micros() > e) {
          NOP();
        }
      }
      while (micros() < e) {
        NOP();
      }
    }
  }

#endif

  namespace locks {

    std::map<gpio_num_t, std::mutex> _gpio_locks;
    std::map<const char *, std::mutex> _locks;
    std::map<uart_port_t, std::mutex> _uart_locks;

    Guard::Guard(const char *name) : _lock(find(name)) {
      if (_lock)
        _lock->lock();
    }
    Guard::Guard(gpio_num_t pin) : _lock(&get(pin)) {
      if (_lock)
        _lock->lock();
    }
    Guard::~Guard() {
      if (_lock)
        _lock->unlock();
    }

    std::mutex &get(const char *name) {
      auto it = _locks.find(name);
      if (it != _locks.end())
        return it->second;
      return _locks[name];
    }

    std::mutex *find(const char *name) {
      auto it = _locks.find(name);
      if (it != _locks.end())
        return &it->second;
      return nullptr;
    }

    std::mutex &get(gpio_num_t pin) {
      auto it = _gpio_locks.find(pin);
      if (it != _gpio_locks.end())
        return it->second;
      return _gpio_locks[pin];
    }

    std::mutex *find(gpio_num_t pin) {
      auto it = _gpio_locks.find(pin);
      if (it != _gpio_locks.end())
        return &it->second;
      return nullptr;
    }

    std::mutex &uart(uart_port_t port) {
      auto it = _uart_locks.find(port);
      if (it != _uart_locks.end())
        return it->second;
      return _uart_locks[port];
    }
  }  // namespace locks

}  // namespace esp32m