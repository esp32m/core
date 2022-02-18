#pragma once

#include <math.h>
#include <driver/uart.h>

#include "esp32m/device.hpp"

namespace esp32m {

  namespace dev {

    class MhZ19b : public virtual Device {
     public:
      MhZ19b(uart_port_t uart_num, const char *name);
      MhZ19b(const MhZ19b &) = delete;
      const char *name() const override {
        return _name ? _name : "MH-Z19B";
      }

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool pollSensors() override;
      bool initSensors() override;

     private:
      const char *_name;
      float _value = NAN;
      unsigned long _stamp = 0;
    };

    MhZ19b *useMhZ19b(uart_port_t uart_num = 2);

  }  // namespace dev
}  // namespace esp32m