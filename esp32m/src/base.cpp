#include "esp32m/base.hpp"

#include <esp_attr.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <cstdarg>
#include <cstdio>
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

  const char* makeTaskName(const char* name) {
    if (!name)
      return "m/generic";
    size_t l = strlen(name) + 3;
    if (l > CONFIG_FREERTOS_MAX_TASK_NAME_LEN)
      l = CONFIG_FREERTOS_MAX_TASK_NAME_LEN;
    char* buf = (char*)calloc(l, 1);
    snprintf(buf, l, "m/%s", name);
    return buf;
  }

  void hex_encode(char* dst, size_t dst_len, const uint8_t* src,
                  size_t src_len) {
    static const char* hex = "0123456789abcdef";
    // dst_len must be >= (src_len*2 + 1)
    size_t j = 0;
    for (size_t i = 0; i < src_len && (j + 2) < dst_len; i++) {
      dst[j++] = hex[(src[i] >> 4) & 0xF];
      dst[j++] = hex[(src[i] >> 0) & 0xF];
    }
    if (j < dst_len)
      dst[j] = '\0';
  }

  std::string hex_encode(const uint8_t* src, size_t src_len) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(src_len * 2);
    for (size_t i = 0; i < src_len; i++) {
      const uint8_t b = src[i];
      out.push_back(hex[(b >> 4) & 0x0F]);
      out.push_back(hex[(b >> 0) & 0x0F]);
    }
    return out;
  }

  bool strEndsWith(const char* str, const char* suffix) {
    if (!str || !suffix)
      return false;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
      return false;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
  }

  std::string string_printf(const char* format, va_list args) {
    if (!format)
      return std::string();

    va_list args_len;
    va_copy(args_len, args);
    const int required = vsnprintf(nullptr, 0, format, args_len);
    va_end(args_len);

    if (required < 0)
      return std::string();

    const size_t requiredSize = static_cast<size_t>(required) + 1;  // incl '\0'

    // Fast path: avoid heap allocation for small formatted strings.
    char stackBuf[128];
    if (requiredSize <= sizeof(stackBuf)) {
      va_list args_print;
      va_copy(args_print, args);
      vsnprintf(stackBuf, sizeof(stackBuf), format, args_print);
      va_end(args_print);
      return std::string(stackBuf, static_cast<size_t>(required));
    }

    std::string result;
    result.resize(static_cast<size_t>(required));
    va_list args_print;
    va_copy(args_print, args);
    vsnprintf(&result[0], requiredSize, format, args_print);
    va_end(args_print);
    return result;
  }

  std::string string_printf(const char* format, ...) {
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

  namespace locks {

    std::map<gpio_num_t, std::mutex> _gpio_locks;
    std::map<std::string, std::mutex> _locks;
    std::map<uart_port_t, std::mutex> _uart_locks;

    Guard::Guard(const char* name) : _lock(find(name)) {
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

    std::mutex& get(const char* name) {
      auto it = _locks.find(name);
      if (it != _locks.end())
        return it->second;
      return _locks[name];
    }

    std::mutex* find(const char* name) {
      auto it = _locks.find(name);
      if (it != _locks.end())
        return &it->second;
      return nullptr;
    }

    std::mutex& get(gpio_num_t pin) {
      auto it = _gpio_locks.find(pin);
      if (it != _gpio_locks.end())
        return it->second;
      return _gpio_locks[pin];
    }

    std::mutex* find(gpio_num_t pin) {
      auto it = _gpio_locks.find(pin);
      if (it != _gpio_locks.end())
        return &it->second;
      return nullptr;
    }

    std::mutex& uart(uart_port_t port) {
      auto it = _uart_locks.find(port);
      if (it != _uart_locks.end())
        return it->second;
      return _uart_locks[port];
    }
  }  // namespace locks

}  // namespace esp32m