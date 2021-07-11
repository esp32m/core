
#include "esp32m/io/pcf857x.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    class PcfPin : public IPin {
     public:
      PcfPin(Pcf857x *owner, int id) : IPin(id), _owner(owner){};
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
      Pcf857x *_owner;
    };

    Pcf857x::Pcf857x(Flavor f, I2C *i2c)
        : IPins(_flavor == Flavor::PCF8574 ? 8 : 16), _flavor(f), _i2c(i2c) {
      _i2c->setErrSnooze(10000);
    }

    IPin *Pcf857x::pin(int num) {
      if (num < 0 || num >= (_flavor == Flavor::PCF8574 ? 8 : 16))
        return nullptr;
      auto id = num + _pinBase;
      std::lock_guard guard(_pinsMutex);
      auto pin = _pins.find(id);
      if (pin == _pins.end()) {
        auto gpio = new PcfPin(this, id);
        _pins[id] = gpio;
        return gpio;
      }
      return pin->second;
    }

    esp_err_t Pcf857x::read(uint16_t &port) {
      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(
          _i2c->read(nullptr, 0, &port, _flavor == Flavor::PCF8574 ? 1 : 2));
      _port = port;
      return ESP_OK;
    }

    esp_err_t Pcf857x::write(uint16_t port) {
      std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(
          _i2c->write(nullptr, 0, &port, _flavor == Flavor::PCF8574 ? 1 : 2));
      _port = port;
      return ESP_OK;
    }

    esp_err_t Pcf857x::id2Pin(int &pin) {
      auto p = pin - _pinBase;
      if (p < 0 || p >= (_flavor == Flavor::PCF8574 ? 8 : 16))
        return ESP_ERR_INVALID_ARG;
      pin = p;
      return ESP_OK;
    }

    esp_err_t Pcf857x::readPin(int pin, bool &value) {
      ESP_CHECK_RETURN(id2Pin(pin));
      auto tx = pin::Tx::current();
      if (!tx || ((tx->type() & pin::Tx::Type::Read) != 0 &&
                  !tx->getReadPerformed())) {
        uint16_t port;
        ESP_CHECK_RETURN(read(port));
        if (tx)
          tx->setReadPerformed();
      }
      value = _port & (1 << pin);
      return ESP_OK;
    }
    esp_err_t Pcf857x::writePin(int pin, bool value) {
      ESP_CHECK_RETURN(id2Pin(pin));
      auto tx = pin::Tx::current();
      if (tx && (tx->type() & pin::Tx::Type::Read) != 0 &&
          !tx->getReadPerformed()) {
        uint16_t port;
        ESP_CHECK_RETURN(read(port));
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
    esp_err_t Pcf857x::setPinMode(int pin, bool input) {
      ESP_CHECK_RETURN(id2Pin(pin));
      if (input)
        _inputMap |= (1 << pin);
      else
        _inputMap &= ~(1 << pin);
      return ESP_OK;
    }

    esp_err_t Pcf857x::commit() {
      return write(_port | _inputMap);
    }

    Pcf857x *usePcf8574(uint8_t addr) {
      return new Pcf857x(Pcf857x::Flavor::PCF8574, new I2C(addr));
    }
    Pcf857x *usePcf8575(uint8_t addr) {
      return new Pcf857x(Pcf857x::Flavor::PCF8575, new I2C(addr));
    }

  }  // namespace io
}  // namespace esp32m