
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

    Pca95x5::Pca95x5(I2C *i2c) : _i2c(i2c) {
      init();
    }

    esp_err_t Pca95x5::read(pca95x5::Register reg, uint16_t &value) {
      return _i2c->readSafe((uint8_t)reg, value);
    }
    esp_err_t Pca95x5::write(pca95x5::Register reg, uint16_t value) {
      return _i2c->writeSafe((uint8_t)reg, value);
    }

    esp_err_t Pca95x5::init() {
      IPins::init(16);
      _i2c->setErrSnooze(10000);
      _i2c->setEndianness(Endian::Little);
      ESP_CHECK_RETURN(read(pca95x5::Register::Input, _port));
      ESP_CHECK_RETURN(read(pca95x5::Register::Config, _inputMap));
      return ESP_OK;
    }

    IPin *Pca95x5::newPin(int id) {
      return new pca95x5::Pin(this, id);
    }

    esp_err_t Pca95x5::readPin(int pin, bool &value) {
      auto tx = pin::Tx::current();
      if (!tx || ((tx->type() & pin::Tx::Type::Read) != 0 &&
                  !tx->getReadPerformed())) {
        ESP_CHECK_RETURN(read(pca95x5::Register::Input, _port));
        // logD("readPin %d reg_input=0x%04x", pin, _port);
        if (tx)
          tx->setReadPerformed();
      }
      value = _port & (1 << pin);
      return ESP_OK;
    }
    esp_err_t Pca95x5::writePin(int pin, bool value) {
      auto tx = pin::Tx::current();
      if (tx && (tx->type() & pin::Tx::Type::Read) != 0 &&
          !tx->getReadPerformed()) {
        ESP_CHECK_RETURN(read(pca95x5::Register::Input, _port));
        // logD("writePin (%d=%d) reg_input=0x%04x", pin, value, _port);
        tx->setReadPerformed();
      }
      if (value)
        _port |= (1 << pin);
      else
        _port &= ~(1 << pin);
      if (!tx || ((tx->type() & pin::Tx::Type::Write) == 0))
        return commit();
      if (!tx->getFinalizer())
        tx->setFinalizer(this);
      return ESP_OK;
    }
    esp_err_t Pca95x5::setPinMode(int pin, bool input) {
      if (input)
        _inputMap |= (1 << pin);
      else
        _inputMap &= ~(1 << pin);
      ESP_CHECK_RETURN(write(pca95x5::Register::Config, _inputMap));
      // logD("setPinMode (%d=%d) reg_config=0x%04x", pin, input, _inputMap);
      return ESP_OK;
    }

    esp_err_t Pca95x5::commit() {
      ESP_CHECK_RETURN(write(pca95x5::Register::Config, _inputMap));
      ESP_CHECK_RETURN(write(pca95x5::Register::Output, _port));

      /*uint16_t o, i, c, inv;
      ESP_CHECK_RETURN(_i2c->readSafe(RegOutput, o));
      ESP_CHECK_RETURN(_i2c->readSafe(RegInput, i));
      ESP_CHECK_RETURN(_i2c->readSafe(RegConfig, c));
      ESP_CHECK_RETURN(_i2c->readSafe(RegInversion, inv));
      logD("commit port=0x%04x, out=0x%04x, in=0x%04x, cfg=0x%04x, inv=0x%04x",
           _port, o, i, c, inv);*/

      return ESP_OK;
    }

    Pca95x5 *usePca95x5(uint8_t addr) {
      return new Pca95x5(new I2C(addr));
    }

  }  // namespace io
}  // namespace esp32m