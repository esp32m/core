
#include "esp32m/io/pcf857x.hpp"
#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    namespace pcf857x {

      class Pin : public IPin {
       public:
        Pin(Pcf857x *owner, int id) : IPin(id), _owner(owner) {
          snprintf(_name, sizeof(_name), "PCF857x-%02u", id);
        };
        const char *name() const override {
          return _name;
        }
        io::pin::Flags flags() override {
          return io::pin::Flags::Input |
                 io::pin::Flags::Output | io::pin::Flags::PullUp;
        }
        esp_err_t createFeature(pin::Type type,
                                pin::Feature **feature) override;

       private:
        Pcf857x *_owner;
        char _name[11];
        friend class Digital;
      };

      class Digital : public io::pin::IDigital {
       public:
        Digital(Pin *pin) : _pin(pin) {}
        /*esp_err_t setDirection(gpio_mode_t mode) override {
          switch (mode) {
            case GPIO_MODE_INPUT:
              _pin->_owner->setPinMode(_pin->id(), true);
              return ESP_OK;
            case GPIO_MODE_OUTPUT:
            case GPIO_MODE_OUTPUT_OD:
            case GPIO_MODE_INPUT_OUTPUT:
            case GPIO_MODE_INPUT_OUTPUT_OD:
              _pin->_owner->setPinMode(_pin->id(), false);
              return ESP_OK;
            default:
              return ESP_FAIL;
          }
        }
        esp_err_t setPull(gpio_pull_mode_t pull) override {
          if (pull == GPIO_PULLUP_ONLY)
            return ESP_OK;
          return ESP_FAIL;
        }*/
        esp_err_t setMode(pin::Mode mode) override {
          mode = deduce(mode, _pin->flags());
          bool input = ((mode & pin::Mode::Output) == 0);
          ESP_CHECK_RETURN(
              _pin->_owner->setPinMode(_pin->id(), input));
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
    }  // namespace pcf857x

    Pcf857x::Pcf857x(Flavor f, i2c::MasterDev *i2c) : _flavor(f), _i2c(i2c) {
      init();
    }

    esp_err_t Pcf857x::init() {
      IPins::init(_flavor == Flavor::PCF8574 ? 8 : 16);
//      _i2c->setErrSnooze(10000);
      return ESP_OK;
    }

    IPin *Pcf857x::newPin(int id) {
      return new pcf857x::Pin(this, id);
    }

    esp_err_t Pcf857x::read(uint16_t &port) {
      // std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(
          _i2c->read(nullptr, 0, &port, _flavor == Flavor::PCF8574 ? 1 : 2));
      _port = port;
      return ESP_OK;
    }

    esp_err_t Pcf857x::write(uint16_t port) {
      // std::lock_guard guard(_i2c->mutex());
      ESP_CHECK_RETURN(
          _i2c->write(nullptr, 0, &port, _flavor == Flavor::PCF8574 ? 1 : 2));
      _port = port;
      return ESP_OK;
    }

    esp_err_t Pcf857x::readPin(int pin, bool &value) {
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
      return new Pcf857x(Pcf857x::Flavor::PCF8574, i2c::MasterDev::create(addr));
    }
    Pcf857x *usePcf8575(uint8_t addr) {
      return new Pcf857x(Pcf857x::Flavor::PCF8575, i2c::MasterDev::create(addr));
    }

  }  // namespace io
}  // namespace esp32m