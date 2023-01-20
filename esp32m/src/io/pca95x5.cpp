
#include "esp32m/io/pca95x5.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    class PcaPin : public IPin {
     public:
      PcaPin(Pca95x5 *owner, int id) : IPin(id), _owner(owner) {
        snprintf(_name, sizeof(_name), "PCA95x5");
      };
      const char *name() const override {
        return _name;
      }
      Features features() override {
        return Features::DigitalInput | Features::DigitalOutput |
               Features::PullUp;
      }
      esp_err_t setDirection(gpio_mode_t mode) override {
        switch (mode) {
          case GPIO_MODE_INPUT:
            _owner->setPinMode(_id, true);
            return ESP_OK;
          case GPIO_MODE_OUTPUT:
          case GPIO_MODE_OUTPUT_OD:
          case GPIO_MODE_INPUT_OUTPUT:
          case GPIO_MODE_INPUT_OUTPUT_OD:
            _owner->setPinMode(_id, false);
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
      esp_err_t digitalRead(bool &value) override {
        return _owner->readPin(_id, value);
      }
      esp_err_t digitalWrite(bool value) override {
        return _owner->writePin(_id, value);
      }

     private:
      Pca95x5 *_owner;
      char _name[11];
    };

    const uint8_t RegInput = 0;
    const uint8_t RegOutput = 2;
    const uint8_t RegInversion = 4;
    const uint8_t RegConfig = 6;

    Pca95x5::Pca95x5(I2C *i2c) : _i2c(i2c) {
      init();
    }

    esp_err_t Pca95x5::init() {
      IPins::init(16);
      _i2c->setErrSnooze(10000);
      _i2c->setEndianness(HostEndianness);
      ESP_CHECK_RETURN(_i2c->readSafe(RegInput, _port));
      ESP_CHECK_RETURN(_i2c->readSafe(RegConfig, _inputMap));
      return ESP_OK;
    }

    IPin *Pca95x5::newPin(int id) {
      return new PcaPin(this, id);
    }

    esp_err_t Pca95x5::readPin(int pin, bool &value) {
      pin = id2num(pin);
      auto tx = pin::Tx::current();
      if (!tx || ((tx->type() & pin::Tx::Type::Read) != 0 &&
                  !tx->getReadPerformed())) {
        ESP_CHECK_RETURN(_i2c->readSafe(RegInput, _port));
        if (tx)
          tx->setReadPerformed();
      }
      value = _port & (1 << pin);
      return ESP_OK;
    }
    esp_err_t Pca95x5::writePin(int pin, bool value) {
      pin = id2num(pin);
      auto tx = pin::Tx::current();
      if (tx && (tx->type() & pin::Tx::Type::Read) != 0 &&
          !tx->getReadPerformed()) {
        ESP_CHECK_RETURN(_i2c->readSafe(RegInput, _port));
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
      pin = id2num(pin);
      if (input)
        _inputMap |= (1 << pin);
      else
        _inputMap &= ~(1 << pin);
      ESP_CHECK_RETURN(_i2c->writeSafe(RegConfig, _inputMap));
      return ESP_OK;
    }

    esp_err_t Pca95x5::commit() {
      ESP_CHECK_RETURN(_i2c->writeSafe(RegOutput, _port));
      return ESP_OK;
    }

    Pca95x5 *usePca95x5(uint8_t addr) {
      return new Pca95x5(new I2C(addr));
    }

  }  // namespace io
}  // namespace esp32m