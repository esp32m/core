#include "esp32m/base.hpp"

#include <esp_attr.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <map>
#include <string>

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
    vTaskDelay(pdMS_TO_TICKS(ms));
  }

  void IRAM_ATTR delayUs(uint32_t us) {
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

  long map(long x, long in_min, long in_max, long out_min, long out_max) {
    const long dividend = out_max - out_min;
    const long divisor = in_max - in_min;
    const long delta = x - in_min;
    if (divisor == 0)
      return -1;  // AVR returns -1, SAM returns 0
    return (delta * dividend + (divisor / 2)) / divisor + out_min;
  }

#endif
  const char *makeTaskName(const char *name) {
    if (!name)
      return "m/generic";
    size_t l = strlen(name) + 3;
    if (l > CONFIG_FREERTOS_MAX_TASK_NAME_LEN)
      l = CONFIG_FREERTOS_MAX_TASK_NAME_LEN;
    char *buf = (char *)calloc(l, 1);
    snprintf(buf, l, "m/%s", name);
    return buf;
  }

  namespace locks {

    std::map<gpio_num_t, std::mutex> _gpio_locks;
    std::map<std::string, std::mutex> _locks;
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