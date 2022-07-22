#pragma once

#include <esp32m/io/pins.hpp>
#include <esp_adc/adc_oneshot.h>

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
    
    esp_err_t getADCUnitHandle(adc_unit_t unit, adc_oneshot_unit_handle_t *ret_unit);
  }
}  // namespace esp32m