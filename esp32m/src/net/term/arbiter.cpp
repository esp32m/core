#include "esp32m/net/term/arbiter.hpp"

#include "esp32m/net/term/uart.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {
  namespace net {
    namespace term {

      Arbiter &Arbiter::instance() {
        static Arbiter i;
        return i;
      }

      Arbiter::Arbiter() {
        _mutex = xSemaphoreCreateMutex();
      }

      esp_err_t Arbiter::acquire(const void *owner, const char *ownerName) {
        if (!owner || !ownerName)
          return ESP_ERR_INVALID_ARG;
        if (!_mutex)
          return ESP_ERR_NO_MEM;

        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
          return ESP_FAIL;

        esp_err_t res = ESP_OK;
        if (_owner && _owner != owner) {
          res = ESP_ERR_INVALID_STATE;
        } else {
          _owner = owner;
          _ownerName = ownerName;
        }

        xSemaphoreGive(_mutex);
        return res;
      }

      void Arbiter::release(const void *owner) {
        if (!owner || !_mutex)
          return;

        if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE)
          return;

        const bool shouldReset = (_owner == owner);
        if (shouldReset) {
          _owner = nullptr;
          _ownerName = nullptr;
        }

        xSemaphoreGive(_mutex);

        if (shouldReset) {
          const esp_err_t err = Uart::instance().resetToDefaults();
          if (err != ESP_OK)
            logW("UART resetToDefaults failed: %s", esp_err_to_name(err));
        }
      }

    }  // namespace term
  }  // namespace io
}  // namespace esp32m
