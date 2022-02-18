#pragma once

#include <esp32m/events/diag.hpp>

#include <map>
#include <mutex>

namespace esp32m {
  namespace debug {
    class Diag {
     public:
      Diag(const Diag &) = delete;
      static Diag &instance();
      int toArray(uint8_t *arr, size_t size);

     private:
      Diag();
      std::map<uint8_t, uint8_t> _map;
      std::mutex _mutex;
    };
  }  // namespace debug
}  // namespace esp32m
