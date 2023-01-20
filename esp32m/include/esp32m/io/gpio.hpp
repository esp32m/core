#pragma once

#include <esp_adc/adc_oneshot.h>
#include <esp32m/io/pins.hpp>
#include <mutex>

namespace esp32m {
  namespace io {
    class Gpio : public IPins {
     public:
      static Gpio &instance();

     protected:
      io::IPin *newPin(int id) override;

     private:
      Gpio();
    };
  }  // namespace io

  namespace gpio {
    io::IPin *pin(gpio_num_t n);

    esp_err_t getADCUnitHandle(adc_unit_t unit,
                               adc_oneshot_unit_handle_t *ret_unit);
    std::mutex &getADCUnitMutex(adc_unit_t unit);
  }  // namespace gpio
}  // namespace esp32m