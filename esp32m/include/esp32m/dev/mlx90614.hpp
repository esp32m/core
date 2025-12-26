#pragma once

#include <memory>

#include "esp32m/bus/i2c/master.hpp"
#include "esp32m/device.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {

  namespace mlx90614 {

    static const uint8_t DefaultAddress = 0x5a;

    enum Register {
      Ta = 0x06,
      TObj1 = 0x07,
      TObj2 = 0x08,
      ToMax = 0x20,
      ToMin = 0x21,
      PwmCtrl = 0x22,
      TaRange = 0x23,
      Emissivity = 0x24,
      Config = 0x25,
      SMBAddr = 0x2E,
      ID1 = 0x3C,
      ID2 = 0x3D,
      ID3 = 0x3E,
      ID4 = 0x3F,
      Flags = 0xF0,
      Sleep = 0xFF
    };

    class Core : public virtual log::Loggable {
     public:
      Core(i2c::MasterDev *i2c, const char *name = nullptr);
      Core(const Core &) = delete;
      const char *name() const override {
        return _name ? _name : "MLX90614";
      }
      esp_err_t init();
      esp_err_t getEmissivity(float &value);
      esp_err_t getObjectTemperature(float &value);
      esp_err_t getAmbientTemperature(float &value);

     protected:
      std::unique_ptr<i2c::MasterDev> _i2c;
      esp_err_t readTemp(Register reg, float &temp);

     private:
      const char *_name;
      float _lsbI = 0, _lsbP = 0;
      esp_err_t read(Register reg, uint16_t &value);
      esp_err_t write(Register reg, uint16_t value);
    };

  }  // namespace mlx90614

  namespace dev {
    class Mlx90614 : public virtual Device, public virtual mlx90614::Core {
     public:
      Mlx90614(i2c::MasterDev *i2c);
      Mlx90614(const Mlx90614 &) = delete;

     protected:
      JsonDocument *getState(RequestContext &ctx) override;
      bool pollSensors() override;
      bool initSensors() override;

     private:
      unsigned long _stamp = 0;
      Sensor _objectTemperature, _ambientTemperature;
    };

    Mlx90614 *useMlx90614(uint8_t addr = mlx90614::DefaultAddress);

  }  // namespace dev
}  // namespace esp32m
