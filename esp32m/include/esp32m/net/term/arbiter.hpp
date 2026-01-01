#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      class Arbiter : public log::Loggable {
       public:
        Arbiter(const Arbiter &) = delete;
        Arbiter &operator=(const Arbiter &) = delete;

        static Arbiter &instance();

        const char *name() const override {
          return "term-arbiter";
        }

        esp_err_t acquire(const void *owner, const char *ownerName);
        void release(const void *owner);

        bool ownedBy(const void *owner) const {
          return owner && owner == _owner;
        }

        const char *ownerName() const {
          return _ownerName ? _ownerName : "";
        }

       private:
        Arbiter();

        SemaphoreHandle_t _mutex = nullptr;
        const void *_owner = nullptr;
        const char *_ownerName = nullptr;
      };

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
