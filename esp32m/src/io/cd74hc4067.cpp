
#include "esp32m/io/cd74hc4067.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace io {

    namespace cd74hc4067 {
      class Pin : public IPin {
       public:
        Pin(CD74HC4067 *owner, int id) : IPin(id), _owner(owner) {
          snprintf(_name, sizeof(_name), "CD74HC4067-%02u", id);
        };
        const char *name() const override {
          return _name;
        }
        pin::Flags flags() override {
          return _owner->_sig->flags();
        }
        esp_err_t createFeature(pin::Type type,
                                pin::Feature **feature) override;

        esp_err_t select() {
          return _owner->selectChannel(_id);
        }

       private:
        CD74HC4067 *_owner;
        char _name[14];
        friend class ADC;
        friend class Digital;
      };

      class Digital : public io::pin::IDigital {
       public:
        Digital(Pin *pin) : _pin(pin) {}
        esp_err_t setDirection(gpio_mode_t mode) override {
          switch (mode) {
            case GPIO_MODE_INPUT:
            case GPIO_MODE_OUTPUT:
            case GPIO_MODE_INPUT_OUTPUT:
              _mode = mode;
              return ESP_OK;
            default:
              return ESP_FAIL;
          }
        }
        esp_err_t setPull(gpio_pull_mode_t pull) override {
          _pull = pull;
          return ESP_OK;
        }
        esp_err_t read(bool &value) override {
          /*if (_mode == GPIO_MODE_INPUT_OUTPUT && _set) {
            value = _value;
            return ESP_OK;
          }*/
          auto sig = _pin->_owner->_sig;
          std::lock_guard guard(sig->mutex());
          auto sd = sig->digital();
          ESP_CHECK_RETURN(_pin->_owner->enable(false));
          ESP_CHECK_RETURN(_pin->select());
          ESP_CHECK_RETURN(_pin->_owner->enable(true));
          ESP_CHECK_RETURN(sd->setDirection(_mode));
          ESP_CHECK_RETURN(sd->setPull(_pull));
          ESP_CHECK_RETURN(sd->read(value));
          return ESP_OK;
        }
        esp_err_t write(bool value) override {
          auto sig = _pin->_owner->_sig;
          std::lock_guard guard(sig->mutex());
          auto sd = sig->digital();
          ESP_CHECK_RETURN(_pin->_owner->enable(false));
          ESP_CHECK_RETURN(_pin->select());
          ESP_CHECK_RETURN(sd->setDirection(_mode));
          ESP_CHECK_RETURN(sd->write(value));
          ESP_CHECK_RETURN(_pin->_owner->enable(true));
          if (_mode == GPIO_MODE_INPUT_OUTPUT) {
            _set = true;
            _value = value;
          }
          return ESP_OK;
        }

       private:
        Pin *_pin;
        gpio_mode_t _mode = GPIO_MODE_INPUT;
        gpio_pull_mode_t _pull = GPIO_FLOATING;
        bool _value = false, _set = false;
      };

      class ADC : public pin::IADC {
       public:
        ADC(Pin *pin) : _pin(pin) {}
        esp_err_t read(int &value, uint32_t *mv) override;
        esp_err_t range(int &min, int &max, uint32_t *mvMin = nullptr,
                        uint32_t *mvMax = nullptr) override;
        adc_atten_t getAtten() override {
          return _atten;
        }
        esp_err_t setAtten(adc_atten_t atten) override {
          _atten = atten;
          return ESP_OK;
        }
        int getWidth() override {
          return _width;
        }
        esp_err_t setWidth(adc_bitwidth_t width) override {
          _width = width;
          return ESP_OK;
        }

       private:
        Pin *_pin;
        adc_bitwidth_t _width = ADC_BITWIDTH_12;
        adc_atten_t _atten = ADC_ATTEN_DB_11;
      };

      esp_err_t Pin::createFeature(pin::Type type, pin::Feature **feature) {
        auto *sig = _owner->_sig;
        switch (type) {
          case pin::Type::Digital:
            if ((sig->flags() & (io::pin::Flags::DigitalInput |
                                    io::pin::Flags::DigitalOutput)) != 0) {
              *feature = new Digital(this);
              return ESP_OK;
            }
            break;
          case pin::Type::ADC:
            if ((sig->flags() & io::pin::Flags::ADC) != 0) {
              *feature = new ADC(this);
              return ESP_OK;
            }
            break;
          default:
            break;
        }
        return IPin::createFeature(type, feature);
      }
    }  // namespace cd74hc4067

    CD74HC4067::CD74HC4067(io::pin::IDigital *pinEn, io::pin::IDigital *pinS0,
                           io::pin::IDigital *pinS1, io::pin::IDigital *pinS2,
                           io::pin::IDigital *pinS3, IPin *pinSig)
        : _en(pinEn),
          _s0(pinS0),
          _s1(pinS1),
          _s2(pinS2),
          _s3(pinS3),
          _sig(pinSig) {
      init();
    }

    esp_err_t CD74HC4067::init() {
      if (_en)
        ESP_CHECK_RETURN(_en->setDirection(GPIO_MODE_OUTPUT));
      ESP_CHECK_RETURN(_s0->setDirection(GPIO_MODE_OUTPUT));
      ESP_CHECK_RETURN(_s1->setDirection(GPIO_MODE_OUTPUT));
      ESP_CHECK_RETURN(_s2->setDirection(GPIO_MODE_OUTPUT));
      ESP_CHECK_RETURN(_s3->setDirection(GPIO_MODE_OUTPUT));
      IPins::init(16);
      return ESP_OK;
    }

    esp_err_t CD74HC4067::enable(bool en) {
      if (_en)
        ESP_CHECK_RETURN(_en->write(!en));
      return ESP_OK;
    }

    IPin *CD74HC4067::newPin(int id) {
      return new cd74hc4067::Pin(this, id);
    }

    esp_err_t CD74HC4067::selectChannel(int c) {
      if (c < 0 || c > 15)
        return ESP_ERR_INVALID_ARG;
      esp_err_t err = ESP_OK;
      pin::Tx tx(pin::Tx::Type::Write, &err);
      ESP_CHECK_RETURN(_s0->write((c & 0x1) != 0));
      ESP_CHECK_RETURN(_s1->write((c & 0x2) != 0));
      ESP_CHECK_RETURN(_s2->write((c & 0x4) != 0));
      ESP_CHECK_RETURN(_s3->write((c & 0x8) != 0));
      return err;
    }

    namespace cd74hc4067 {
      esp_err_t ADC::read(int &value, uint32_t *mv) {
        auto sig = _pin->_owner->_sig;
        auto adc = sig->adc();
        if (!adc)
          return ESP_FAIL;
        std::lock_guard guard(sig->mutex());
        ESP_CHECK_RETURN(adc->setWidth(_width));
        ESP_CHECK_RETURN(adc->setAtten(_atten));
        ESP_CHECK_RETURN(_pin->_owner->enable(false));
        ESP_CHECK_RETURN(_pin->select());
        ESP_CHECK_RETURN(_pin->_owner->enable(true));
        return adc->read(value, mv);
      }
      esp_err_t ADC::range(int &min, int &max, uint32_t *mvMin,
                           uint32_t *mvMax) {
        auto sig = _pin->_owner->_sig;
        auto adc = sig->adc();
        if (!adc)
          return ESP_FAIL;
        return adc->range(min, max, mvMin, mvMax);
      }

    }  // namespace cd74hc4067

    CD74HC4067 *useCD74HC4067(IPin *pinEn, IPin *pinS0, IPin *pinS1,
                              IPin *pinS2, IPin *pinS3, IPin *pinSig) {
      return new CD74HC4067(pinEn ? pinEn->digital() : nullptr,
                            pinS0->digital(), pinS1->digital(),
                            pinS2->digital(), pinS3->digital(), pinSig);
    }

  }  // namespace io
}  // namespace esp32m