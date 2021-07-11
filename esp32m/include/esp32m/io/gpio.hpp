#pragma once

#include <esp32m/io/pins.hpp>

namespace esp32m {
  namespace io {
    class Gpio : public IPins {
     public:
      io::IPin *pin(int num) override;
      static Gpio &instance();

     private:
      Gpio();
    };
  }  // namespace io

  namespace gpio {
    io::IPin *pin(gpio_num_t n);
  }
}  // namespace esp32m