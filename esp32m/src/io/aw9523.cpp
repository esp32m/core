#include "esp32m/io/aw9523.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    namespace aw9523 {

      enum Register {
        InP0,  ///< Register for reading input values
        InP1,
        OutP0,  ///< Register for writing output values
        OutP1,
        CfgP0,  ///< Register for configuring direction
        CfgP1,
        IntP0,  ///< Register for enabling interrupt
        IntP1,
        ChipId = 0x10,  ///< Register for hardcode chip ID
        Gcr,            ///< Register for general configuration
        ModeP0,         ///< Register for configuring const current
        ModeP1,         ///< Register for configuring const current
        Dim0 = 0x20,
        Dim1,
        Dim3,
        Dim4,
        Dim5,
        Dim6,
        Dim7,
        Dim8,
        Dim9,
        Dim10,
        Dim11,
        Dim12,
        Dim13,
        Dim14,
        Dim15,
        Rst = 0x7f,  ///< Register for soft resetting
      };

      class Pin : public IPin {
       public:
        Pin(Aw9523 *owner, int id) : IPin(id), _owner(owner) {
          snprintf(_name, sizeof(_name), "AW9523-%02u", id);
        };
        const char *name() const override {
          return _name;
        }
        pin::Flags flags() override {
          return pin::Flags::DigitalInput | pin::Flags::DigitalOutput |
                 pin::Flags::PullUp | pin::Flags::DAC;
        }

        esp_err_t analogWrite(float value) {
          return _owner->analogWrite(_id, value);
        }

       protected:
        esp_err_t createFeature(pin::Type type,
                                pin::Feature **feature) override;

       private:
        Aw9523 *_owner;
        char _name[11];
        friend class Digital;
      };

      class Digital : public pin::IDigital {
       public:
        Digital(Pin *pin) : _pin(pin) {}

        esp_err_t setDirection(gpio_mode_t mode) override {
          switch (mode) {
            case GPIO_MODE_INPUT:
              _pin->_owner->setPinMode(_pin->id(), PinMode::Input);
              return ESP_OK;
            case GPIO_MODE_OUTPUT:
            case GPIO_MODE_OUTPUT_OD:
            case GPIO_MODE_INPUT_OUTPUT:
            case GPIO_MODE_INPUT_OUTPUT_OD:
              _pin->_owner->setPinMode(_pin->id(), PinMode::Output);
              return ESP_OK;
            default:
              return ESP_FAIL;
          }
        }
        esp_err_t setPull(gpio_pull_mode_t pull) override {
          if (pull == GPIO_PULLUP_ONLY)
            return ESP_OK;
          return ESP_FAIL;
        }
        esp_err_t read(bool &value) override {
          return _pin->_owner->readPin(_pin->id(), value);
        }
        esp_err_t write(bool value) override {
          return _pin->_owner->writePin(_pin->id(), value);
        }

       private:
        Pin *_pin;
      };

      class DAC : public pin::IDAC {
       public:
        DAC(Pin *pin) : _pin(pin) {}
        esp_err_t write(float value) override;

       private:
        Pin *_pin;
      };

      esp_err_t DAC::write(float value) {
        return _pin->analogWrite(value);
      }

      esp_err_t Pin::createFeature(pin::Type type, pin::Feature **feature) {
        switch (type) {
          case pin::Type::Digital:
            *feature = new Digital(this);
            return ESP_OK;
          case pin::Type::DAC:
            *feature = new DAC(this);
            return ESP_OK;
          default:
            break;
        }
        return IPin::createFeature(type, feature);
      }

    }  // namespace aw9523

    Aw9523::Aw9523(I2C *i2c) : _i2c(i2c) {
      init();
    }

    esp_err_t Aw9523::init() {
      _i2c->setEndianness(Endian::Little);
      ESP_CHECK_RETURN(reset(true));
      uint8_t id;
      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(_i2c->read(aw9523::Register::ChipId, id));
      if (id != 0x23) {
        loge("AW9523 chip not detected");
        return ESP_FAIL;
      }
      ESP_CHECK_RETURN(_i2c->read(aw9523::Register::OutP0, _out));
      ESP_CHECK_RETURN(_i2c->read(aw9523::Register::CfgP0, _cfg));
      ESP_CHECK_RETURN(_i2c->read(aw9523::Register::IntP0, _int));
      ESP_CHECK_RETURN(_i2c->read(aw9523::Register::ModeP0, _mode));
      ESP_CHECK_RETURN(_i2c->read(aw9523::Register::Gcr, _gcr));
      IPins::init(16);
      return ESP_OK;
    }

    IPin *Aw9523::newPin(int id) {
      return new aw9523::Pin(this, id);
    }

    esp_err_t Aw9523::setRst(io::IPin *pin) {
      _rst = pin ? pin->digital() : nullptr;
      if (_rst)
        ESP_CHECK_RETURN(_rst->write(true));
      return ESP_OK;
    }

    esp_err_t Aw9523::reset(bool hard) {
      if (hard && _rst) {
        ESP_CHECK_RETURN(_rst->write(false));
        delayUs(20);
        ESP_CHECK_RETURN(_rst->write(true));
      } else {
        std::lock_guard guard(_i2c->mutex());
        ESP_CHECK_RETURN(_i2c->write(aw9523::Register::Rst, (uint8_t)0));
      }
      return ESP_OK;
    }

    esp_err_t Aw9523::analogWrite(int pin, float value) {
      ESP_CHECK_RETURN(setPinMode(pin, aw9523::PinMode::DAC));
      uint8_t reg;
      // See Table 13. 256 step dimming control register
      if ((pin >= 0) && (pin <= 7))
        reg = 0x24 + pin;
      else if ((pin >= 8) && (pin <= 11))
        reg = 0x20 + pin - 8;
      else if ((pin >= 12) && (pin <= 15))
        reg = 0x2C + pin - 12;
      else
        return ESP_ERR_INVALID_ARG;
      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(_i2c->write(reg, (uint8_t)(255 * value)));
      return ESP_OK;
    }

    esp_err_t Aw9523::readPin(int pin, bool &value) {
      uint16_t mask = 1 << pin;
      if (_cfg & mask) {
        auto tx = pin::Tx::current();
        if (!tx || ((tx->type() & pin::Tx::Type::Read) != 0 &&
                    !tx->getReadPerformed())) {
          ESP_CHECK_RETURN(readInput());
          if (tx)
            tx->setReadPerformed();
        }
        value = _input & mask;
      } else if (_mode & mask) {
        value = _out & mask;
      } else
        return ESP_ERR_INVALID_STATE;
      return ESP_OK;
    }

    esp_err_t Aw9523::writePin(int pin, bool value) {
      auto tx = pin::Tx::current();
      if (value)
        _out |= (1 << pin);
      else
        _out &= ~(1 << pin);
      if (!tx || ((tx->type() & pin::Tx::Type::Write) == 0))
        return commit();
      if (!tx->getFinalizer())
        tx->setFinalizer(this);
      return ESP_OK;
    }

    esp_err_t Aw9523::setPinMode(int pin, aw9523::PinMode pm) {
      std::lock_guard guard(_i2c->mutex());
      uint16_t mode = _mode;
      uint16_t cfg = _cfg;
      uint16_t mask = 1 << pin;
      switch (pm) {
        case aw9523::PinMode::Input:
          cfg |= mask;
          mode |= mask;
          break;
        case aw9523::PinMode::Output:
          cfg &= ~mask;
          mode |= mask;
          break;
        case aw9523::PinMode::DAC:
          cfg &= ~mask;
          mode &= ~mask;
          break;
      }
      if (cfg == _cfg && mode == _mode)
        return ESP_OK;
      if (cfg != _cfg)
        ESP_CHECK_RETURN(_i2c->write(aw9523::Register::CfgP0, _cfg = cfg));
      if (mode != _mode)
        ESP_CHECK_RETURN(_i2c->write(aw9523::Register::ModeP0, _mode = mode));
      return ESP_OK;
    }

    esp_err_t Aw9523::readInput() {
      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(_i2c->read(aw9523::Register::InP0, _input));
      return ESP_OK;
    }

    esp_err_t Aw9523::writeOutput() {
      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(_i2c->write(aw9523::Register::OutP0, _out));
      return ESP_OK;
    }

    esp_err_t Aw9523::commit() {
      return writeOutput();
    }
    esp_err_t Aw9523::setP0PushPull(bool enable) {
      uint16_t gcr = _gcr;
      if (enable)
        gcr |= BIT4;
      else
        gcr &= ~BIT4;
      return updateGcr(gcr);
    }
    esp_err_t Aw9523::updateGcr(uint16_t gcr) {
      std::lock_guard guard(_i2c->mutex());
      if (gcr != _gcr)
        ESP_CHECK_RETURN(_i2c->write(aw9523::Register::Gcr, _gcr = gcr));
      return ESP_OK;
    }

    Aw9523 *useAw9523(uint8_t addr) {
      return new Aw9523(new I2C(addr));
    }

  }  // namespace io
}  // namespace esp32m