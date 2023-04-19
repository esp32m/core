#pragma once

#include <ArduinoJson.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <hal/uart_types.h>
#include <string.h>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace esp32m {

  unsigned long micros();
  unsigned long millis();
  void delay(uint32_t ms);
  void delayUs(uint32_t us);
  long map(long x, long in_min, long in_max, long out_min, long out_max);

  const char *makeTaskName(const char *name);
  std::string string_printf(const char *format, va_list args);
  std::string string_printf(const char *format, ...);

  class INamed {
   public:
    virtual const char *name() const = 0;
  };

  template <typename F>
  bool waitState(F state, int ms) {
    for (int i = 0; i < ms; i++)
      if (state())
        return true;
      else
        delay(1);
    return false;
  }


  namespace locks {
    class Guard {
     public:
      Guard(const char *name);
      Guard(gpio_num_t pin);
      ~Guard();

     private:
      std::mutex *_lock;
    };
    std::mutex &get(const char *name);
    std::mutex *find(const char *name);

    std::mutex &get(gpio_num_t pin);
    std::mutex *find(gpio_num_t pin);

    std::mutex &uart(uart_port_t pin);
  }  // namespace locks

  enum Endian { Little, Big };

  static const Endian HostEndianness = Endian::Little;

  namespace endian {
    template <typename T, size_t sz>
    struct swap_bytes {
      inline T operator()(T val) {
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
      }
    };
    template <typename T>
    struct swap_bytes<T, 1> {
      inline T operator()(T val) {
        return val;
      }
    };

    template <typename T>
    struct swap_bytes<T, 2> {
      inline T operator()(T val) {
        return ((((val) >> 8) & 0xff) | (((val)&0xff) << 8));
      }
    };

    template <typename T>
    struct swap_bytes<T, 4> {
      inline T operator()(T val) {
        return ((((val)&0xff000000) >> 24) | (((val)&0x00ff0000) >> 8) |
                (((val)&0x0000ff00) << 8) | (((val)&0x000000ff) << 24));
      }
    };

    template <>
    struct swap_bytes<float, 4> {
      inline float operator()(float val) {
        uint32_t mem =
            swap_bytes<uint32_t, sizeof(uint32_t)>()(*(uint32_t *)&val);
        return *(float *)&mem;
      }
    };

    template <typename T>
    struct swap_bytes<T, 8> {
      inline T operator()(T val) {
        return ((((val)&0xff00000000000000ull) >> 56) |
                (((val)&0x00ff000000000000ull) >> 40) |
                (((val)&0x0000ff0000000000ull) >> 24) |
                (((val)&0x000000ff00000000ull) >> 8) |
                (((val)&0x00000000ff000000ull) << 8) |
                (((val)&0x0000000000ff0000ull) << 24) |
                (((val)&0x000000000000ff00ull) << 40) |
                (((val)&0x00000000000000ffull) << 56));
      }
    };

    template <>
    struct swap_bytes<double, 8> {
      inline double operator()(double val) {
        uint64_t mem =
            swap_bytes<uint64_t, sizeof(uint64_t)>()(*(uint64_t *)&val);
        return *(double *)&mem;
      }
    };

    template <Endian from, Endian to, class T>
    struct do_byte_swap {
      inline T operator()(T value) {
        return swap_bytes<T, sizeof(T)>()(value);
      }
    };
    // specialisations when attempting to swap to the same endianess
    template <class T>
    struct do_byte_swap<Endian::Little, Endian::Little, T> {
      inline T operator()(T value) {
        return value;
      }
    };
    template <class T>
    struct do_byte_swap<Endian::Big, Endian::Big, T> {
      inline T operator()(T value) {
        return value;
      }
    };

  }  // namespace endian

  template <Endian from, Endian to, class T>
  inline T byte_swap(T value) {
    // ensure the data is only 1, 2, 4 or 8 bytes
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 ||
                  sizeof(T) == 8);
    // ensure we're only swapping arithmetic types
    static_assert(std::is_arithmetic<T>::value);

    return endian::do_byte_swap<from, to, T>()(value);
  }

  template <class T>
  inline T fromEndian(Endian from, T value) {
    return from == Endian::Little
               ? byte_swap<Endian::Little, HostEndianness>(value)
               : byte_swap<Endian::Big, HostEndianness>(value);
  }

  template <class T>
  inline T toEndian(Endian to, T value) {
    return to == Endian::Little
               ? byte_swap<HostEndianness, Endian::Little>(value)
               : byte_swap<HostEndianness, Endian::Big>(value);
  }

}  // namespace esp32m