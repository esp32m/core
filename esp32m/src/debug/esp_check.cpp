#include "esp32m/logging.hpp"

#include <esp_err.h>
#include <esp_rom_sys.h>

// These __real__ declarations let us fall back to the original esp_system
// implementation before esp32m logging is ready (e.g. during early boot,
// before App::App() has registered any appenders).
extern "C" {
void __real__esp_error_check_failed_without_abort(esp_err_t rc,
                                                  const char* file, int line,
                                                  const char* function,
                                                  const char* expression);
void __real__esp_error_check_failed(esp_err_t rc, const char* file, int line,
                                    const char* function,
                                    const char* expression);

void __wrap__esp_error_check_failed_without_abort(esp_err_t rc,
                                                  const char* file, int line,
                                                  const char* function,
                                                  const char* expression) {
  if (esp32m::log::hasAppenders()) {
    loge("ESP_ERROR_CHECK failed: 0x%x (%s) at %s:%d in %s (expr: %s)", rc,
         esp_err_to_name(rc), file, line, function, expression);
  } else {
    __real__esp_error_check_failed_without_abort(rc, file, line, function,
                                                 expression);
  }
}

void __wrap__esp_error_check_failed(esp_err_t rc, const char* file, int line,
                                    const char* function,
                                    const char* expression) {
  if (esp32m::log::hasAppenders()) {
    loge("ESP_ERROR_CHECK failed: 0x%x (%s) at %s:%d in %s (expr: %s)", rc,
         esp_err_to_name(rc), file, line, function, expression);
  } else {
    __real__esp_error_check_failed(rc, file, line, function, expression);
  }
  abort();
}
}  // extern "C"
