#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <memory>
#include "hal/spi_types.h"
#include "driver/spi_master.h"

#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace dev {

    class Max6675 : public Device {
     public:
      Max6675(const char *name, spi_host_device_t host);
      Max6675(const Max6675 &) = delete;
      const char *name() const override {
        return _name;
      }

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;

     private:
      const char *_name;
      spi_host_device_t _host;
      spi_device_handle_t _spi=nullptr;
      float _value = NAN;
      unsigned long _stamp = 0;
    };

    Max6675 *useMax6675(const char *name=nullptr, spi_host_device_t host=VSPI_HOST);
  }  // namespace dev
}  // namespace esp32m