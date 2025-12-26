
#include "esp32m/io/pca95x5.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    namespace pca95x5 {

      class Pin : public IPin {
       public:
        Pin(Pca95x5 *owner, int id) : IPin(id), _owner(owner) {
          snprintf(_name, sizeof(_name), "PCA95x5-%02u", id);
        };
        const char *name() const override {
          return _name;
        }
        io::pin::Flags flags() override {
          return io::pin::Flags::Input | io::pin::Flags::Output |
                 io::pin::Flags::PullUp;
        }
        esp_err_t createFeature(pin::Type type,
                                pin::Feature **feature) override;

       private:
        Pca95x5 *_owner;
        char _name[11];
        friend class Digital;
      };

      class Digital : public io::pin::IDigital {
       public:
        Digital(Pin *pin) : _pin(pin) {}
        esp_err_t setMode(pin::Mode mode) override {
          mode = deduce(mode, _pin->flags());
          bool input = ((mode & pin::Mode::Output) == 0);
          ESP_CHECK_RETURN(_pin->_owner->setPinMode(_pin->id(), input));
          _mode = mode;
          return ESP_OK;
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

      esp_err_t Pin::createFeature(pin::Type type, pin::Feature **feature) {
        switch (type) {
          case pin::Type::Digital:
            *feature = new Digital(this);
            return ESP_OK;
          default:
            break;
        }
        return IPin::createFeature(type, feature);
      }
    }  // namespace pca95x5

    Pca95x5::Pca95x5(i2c::MasterDev *i2c, pca95x5::Bits bits) : _i2c(i2c), _bits(bits) {
      init();
    }

    esp_err_t Pca95x5::read(pca95x5::Register reg, uint16_t &value) {
      switch (_bits) {
        case pca95x5::Bits::Eight: {
          uint8_t v8;
          auto result = _i2c->read((uint8_t)reg, v8);
          if (result == ESP_OK)
            value = v8;
        } break;
        case pca95x5::Bits::Sixteen:
          return _i2c->read((uint8_t)reg << 1, value);
      }
      return ESP_FAIL;
    }
    esp_err_t Pca95x5::write(pca95x5::Register reg, uint16_t value) {
      switch (_bits) {
        case pca95x5::Bits::Eight:
          return _i2c->write((uint8_t)reg, (uint8_t)value);
        case pca95x5::Bits::Sixteen:
          return _i2c->write((uint8_t)reg << 1, value);
      }
      return ESP_FAIL;
    }

    esp_err_t Pca95x5::init() {
      IPins::init(_bits == pca95x5::Bits::Eight ? 8 : 16);
      _i2c->setEndianness(Endian::Little);
      ESP_CHECK_RETURN(read(pca95x5::Register::Input, _input));
      ESP_CHECK_RETURN(read(pca95x5::Register::Output, _output));
      ESP_CHECK_RETURN(read(pca95x5::Register::Config, _config));
      _targetOutput = _output;
      return ESP_OK;
    }

    IPin *Pca95x5::newPin(int id) {
      return new pca95x5::Pin(this, id);
    }

    esp_err_t Pca95x5::readPin(int pin, bool &value) {
      auto isOutput = (_config & (1 << pin)) == 0;
      if (isOutput) {
        value = _output & (1 << pin);
        return ESP_OK;
      }

      auto tx = pin::Tx::current();
      if (!tx || ((tx->type() & pin::Tx::Type::Read) != 0 &&
                  !tx->getReadPerformed())) {
        ESP_CHECK_RETURN(read(pca95x5::Register::Input, _input));
        // logD("readPin %d reg_input=0x%04x", pin, _input);
        if (tx)
          tx->setReadPerformed();
      }
      value = _input & (1 << pin);
      return ESP_OK;
    }
    esp_err_t Pca95x5::writePin(int pin, bool value) {
      // logD("writePin %d: %d", pin, value);
      auto tx = pin::Tx::current();
      if (tx && (tx->type() & pin::Tx::Type::Read) != 0 &&
          !tx->getReadPerformed()) {
        ESP_CHECK_RETURN(read(pca95x5::Register::Input, _input));
        tx->setReadPerformed();
      }
      if (value)
        _targetOutput |= (1 << pin);
      else
        _targetOutput &= ~(1 << pin);

      if (!tx || ((tx->type() & pin::Tx::Type::Write) == 0))
        return commit();

      if (!tx->getFinalizer())
        tx->setFinalizer(this);
      return ESP_OK;
    }
    esp_err_t Pca95x5::setPinMode(int pin, bool input) {
      if (input)
        _config |= (1 << pin);
      else
        _config &= ~(1 << pin);
      ESP_CHECK_RETURN(write(pca95x5::Register::Config, _config));
      // logD("setPinMode (%d=%d) reg_config=0x%04x", pin, input, _config);
      return ESP_OK;
    }

    esp_err_t Pca95x5::commit() {
      // ESP_CHECK_RETURN(write(pca95x5::Register::Config, _config));
      if (_targetOutput != _output) {
        ESP_CHECK_RETURN(write(pca95x5::Register::Output, _targetOutput));
        _output = _targetOutput;
        // logD("commit config=0x%04x output=0x%04x", _config, _output);
      }
      /*uint16_t o, i, c, inv;
      ESP_CHECK_RETURN(read(pca95x5::Register::Output, o));
      ESP_CHECK_RETURN(read(pca95x5::Register::Input, i));
      ESP_CHECK_RETURN(read(pca95x5::Register::Config, c));
      ESP_CHECK_RETURN(read(pca95x5::Register::Inversion, inv));
      logD("commit port=0x%04x, out=0x%04x, in=0x%04x, cfg=0x%04x, inv=0x%04x",
           _output, o, i, c, inv);*/

      return ESP_OK;
    }

    Pca95x5 *usePca95x5(uint8_t addr) {
      return new Pca95x5(i2c::MasterDev::create(addr));
    }

  }  // namespace io
}  // namespace esp32m