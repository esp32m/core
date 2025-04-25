#pragma once

#include <driver/uart.h>
#include <math.h>

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
      JsonDocument *getState(RequestContext &ctx) override;
      bool pollSensors() override;
      bool initSensors() override;

     private:
      const char *_name;
      float _value = NAN;
      unsigned long _stamp = 0;
    };

#if SOC_UART_HP_NUM > 2
    MhZ19b *useMhZ19b(uart_port_t uart_num = UART_NUM_2);
#else
    MhZ19b *useMhZ19b(uart_port_t uart_num = UART_NUM_1);
#endif

  }  // namespace dev
}  // namespace esp32m