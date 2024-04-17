#pragma once

#include <ArduinoJson.h>
#include <math.h>
#include <memory>
#include "driver/spi_master.h"
#include "hal/spi_types.h"

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
      spi_device_handle_t _spi = nullptr;
      float _value = NAN;
      unsigned long _stamp = 0;
    };
#if SOC_SPI_PERIPH_NUM > 2
#  define SPI_HOST_MAX6675 SPI3_HOST
#else
#  define SPI_HOST_MAX6675 SPI2_HOST
#endif
    Max6675 *useMax6675(const char *name = nullptr,
                        spi_host_device_t host = SPI_HOST_MAX6675);
  }  // namespace dev
}  // namespace esp32m