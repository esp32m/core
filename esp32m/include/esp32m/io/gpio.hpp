#pragma once

#include <esp32m/io/pins.hpp>

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
  }
}  // namespace esp32m