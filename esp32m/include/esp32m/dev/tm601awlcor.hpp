#pragma once

#include "esp32m/bus/i2c/master.hpp"
#include "esp32m/device.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {

  namespace tm601awlcor {

    const uint8_t DefaultAddress = 0x40;

    enum class Register : uint8_t {
      Level = 0x0,
      Reg1 = 0x1,
      Reg2 = 0x2,
      Reg3 = 0x3,
    };

    class Core : public virtual log::Loggable {
     public:
      Core(const char* name) {
        _name = name ? name : "TM601AWLCOR";
      }
      Core(const Core&) = delete;
      const char* name() const override {
        return _name;
      }
      esp_err_t readLevel(uint8_t& level) {
        return read(Register::Level, level);
      }

     protected:
      std::unique_ptr<i2c::MasterDev> _i2c;
      const char* _name;

      esp_err_t init(i2c::MasterDev* i2c) {
        _i2c.reset(i2c);
        return ESP_OK;
      }

      esp_err_t read(Register reg, uint8_t& value) {
        return _i2c->read(&reg, 1, &value, sizeof(value));
      }

     private:
    };

  }  // namespace tm601awlcor

  namespace dev {
    class Tm601awlcor : public virtual Device,
                        public virtual tm601awlcor::Core {
     public:
      Tm601awlcor(i2c::MasterDev* i2c, const char* name = nullptr)
          : tm601awlcor::Core(name), _level(this, "distance", "level") {
        Device::init(Flags::HasSensors);
        _level.unit = "cm";
        _level.precision = 1;
        tm601awlcor::Core::init(i2c);
      }
      Tm601awlcor(const Tm601awlcor&) = delete;

     protected:
      JsonDocument* getState(RequestContext& ctx) override {
        JsonDocument* doc = new JsonDocument();
        JsonArray arr = doc->to<JsonArray>();
        arr.add(millis() - _stamp);
        arr.add(_level.get());
        return doc;
      }
      JsonDocument* getConfig(RequestContext& ctx) override {
        JsonDocument* doc = new JsonDocument();
        JsonObject obj = doc->to<JsonObject>();
        uint8_t regs[3];
        tm601awlcor::Register reg = tm601awlcor::Register::Reg1;
        if (_i2c->read(&reg, 1, &regs, sizeof(regs)) == ESP_OK) {
          obj["reg1"] = regs[0];
          obj["reg2"] = regs[1];
          obj["reg3"] = regs[2];
        }
        return doc;
      }
      bool setConfig(RequestContext& ctx) override {
        auto data = ctx.data.as<JsonObjectConst>();
        bool changed = false;
        uint8_t regs[3];
        tm601awlcor::Register reg = tm601awlcor::Register::Reg1;
        if (_i2c->read(&reg, 1, &regs, sizeof(regs)) != ESP_OK)
          return false;
        uint8_t v = regs[0];
        if (json::from(data["reg1"], v, &changed))
          regs[0] = v;
        v = regs[1];
        if (json::from(data["reg2"], v, &changed))
          regs[1] = v;
        v = regs[2];
        if (json::from(data["reg3"], v, &changed))
          regs[2] = v;
        if (changed) {
          if (_i2c->write(&reg, 1, &regs, sizeof(regs)) != ESP_OK)
            return false;
        }
        return changed;
      }
      bool initSensors() override {
        return true;
      }
      bool pollSensors() override {
        uint8_t level;
        ESP_CHECK_RETURN_BOOL(readLevel(level));
        bool changed = false;
        _level.set(level, &changed);
        _stamp = millis();
        return true;
      }

     private:
      Sensor _level;
      uint64_t _stamp = 0;
    };

    Tm601awlcor* useTm601awlcor(uint8_t addr = tm601awlcor::DefaultAddress,
                                const char* name = nullptr);
  }  // namespace dev
}  // namespace esp32m