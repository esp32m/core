#include "esp32m/base.hpp"

#include <esp_attr.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <map>

#define NOP() asm volatile("nop")

namespace esp32m {

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

  bool strEndsWith(const char *str, const char *suffix) {
    if (!str || !suffix)
      return false;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
      return false;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
  }

  std::string string_printf(const char *format, va_list args) {
    size_t sz = vsnprintf(nullptr, 0, format, args);
    size_t bufsize = sz + 1;
    char *buf = (char *)malloc(bufsize);
    std::string result;
    if (buf) {
      vsnprintf(buf, bufsize, format, args);
      result = buf;
      free(buf);
    }
    return result;
  }

  std::string string_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    std::string result = string_printf(format, args);
    va_end(args);
    return result;
  }

  float roundTo(float value, int precision) {
    float f = 1;
    switch (precision) {
      case 0:
        break;
      case 1:
        f = 10;
        break;
      case 2:
        f = 100;
        break;
      case 3:
        f = 1000;
        break;
      case 4:
        f = 10000;
        break;

      default:
        f = pow(10, precision);
        break;
    }
    return (int)(value * f + 0.5) / f;
  }

  std::map<int, std::string> precisionFormats;
  std::string roundToString(float value, int precision) {
    auto it = precisionFormats.find(precision);
    std::string fmt;
    if (it == precisionFormats.end())
      fmt = precisionFormats[precision] = string_printf("%%.%df", precision);
    else
      fmt = it->second;
    return string_printf(fmt.c_str(), value);
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