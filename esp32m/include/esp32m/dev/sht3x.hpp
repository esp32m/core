#pragma once

#include "esp32m/bus/i2c/master.hpp"
#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {

  namespace sht3x {

    const uint8_t DefaultAddress = 0x44;
    const size_t RawDataSize = 6;

    enum Mode {
      Single = 0,
      SingleStretched,
      Periodic05Mps,
      Periodic1Mps,
      Periodic2Mps,
      Periodic4Mps,
      Periodic10Mps
    };

    enum Repeatability {
      High = 0,
      Medium,
      Low,
    };

    class Core : public virtual log::Loggable {
     public:
      Core();
      Core(const Core &) = delete;
      const char *name() const override {
        return _name;
      }
      esp_err_t reset(Mode mode, Repeatability repeat);
      esp_err_t setHeater(bool on);
      esp_err_t start();
      esp_err_t stop();
      void waitMeasured();
      bool isMeasuring();
      esp_err_t getReadings(float *temperature, float *humidity);
      esp_err_t measure(float *temperature, float *humidity);

     protected:
      std::unique_ptr<i2c::MasterDev> _i2c;
      const char *_name;
      io::pin::IDigital *_reset = nullptr;
      esp_err_t init(i2c::MasterDev *i2c, io::IPin *resetPin = nullptr,
                     const char *name = nullptr);

     private:
      Mode _mode;
      Repeatability _repeatability;  //!< used repeatability
      uint8_t _data[RawDataSize];

      uint64_t _start;
      bool _first, _dataValid;
      esp_err_t cmd(uint16_t c);
      esp_err_t read();
    };
  }  // namespace sht3x

  namespace dev {
    class Sht3x : public virtual Device, public virtual sht3x::Core {
     public:
      Sht3x(i2c::MasterDev *i2c, io::IPin *resetPin = nullptr, const char *name = nullptr);
      Sht3x(const Sht3x &) = delete;

     protected:
      JsonDocument *getState(RequestContext &ctx) override;
      bool initSensors() override;
      bool pollSensors() override;

     private:
      uint64_t _stamp = 0;
      Sensor _temperature, _humidity;
    };

    Sht3x *useSht3x(uint8_t addr = sht3x::DefaultAddress,
                    io::IPin *resetPin = nullptr, const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m