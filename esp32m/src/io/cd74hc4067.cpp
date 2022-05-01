
#include "esp32m/io/cd74hc4067.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace io {
    class CD74HC4067Pin;

    namespace cd74hc4067 {
      class ADC : public pin::IADC {
       public:
        ADC(CD74HC4067Pin *pin) : _pin(pin) {}
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
        adc_bits_width_t getWidth() override {
          return _width;
        }

        esp_err_t setWidth(adc_bits_width_t width) override {
          _width = width;
          return ESP_OK;
        }

       private:
        CD74HC4067Pin *_pin;
        adc_bits_width_t _width = ADC_WIDTH_BIT_12;
        adc_atten_t _atten = ADC_ATTEN_DB_11;
      };
    }  // namespace cd74hc4067

    class CD74HC4067Pin : public IPin {
     public:
      CD74HC4067Pin(CD74HC4067 *owner, int id) : IPin(id), _owner(owner) {
        snprintf(_name, sizeof(_name), "CD74HC4067-%02u",
                 id - owner->pinBase());
      };
      const char *name() const override {
        return _name;
      }
      Features features() override {
        return _owner->_sig->features();
      }
      esp_err_t setDirection(gpio_mode_t mode) override {
        switch (mode) {
          case GPIO_MODE_INPUT:
            return ESP_OK;
          case GPIO_MODE_OUTPUT:
            return ESP_OK;
          default:
            return ESP_FAIL;
        }
      }
      esp_err_t setPull(gpio_pull_mode_t pull) override {
        return ESP_FAIL;
      }
      esp_err_t digitalRead(bool &value) override {
        auto sig = _owner->_sig;
        std::lock_guard guard(sig->mutex());
        ESP_CHECK_RETURN(_owner->enable(false));
        ESP_CHECK_RETURN(select());
        ESP_CHECK_RETURN(_owner->enable(true));
        ESP_CHECK_RETURN(sig->digitalRead(value));
        return ESP_OK;
      }
      esp_err_t digitalWrite(bool value) override {
        auto sig = _owner->_sig;
        std::lock_guard guard(sig->mutex());
        ESP_CHECK_RETURN(_owner->enable(false));
        ESP_CHECK_RETURN(select());
        ESP_CHECK_RETURN(sig->digitalWrite(value));
        ESP_CHECK_RETURN(_owner->enable(true));
        return ESP_OK;
      }
      pin::Impl *createImpl(pin::Type type) override {
        auto *sig = _owner->_sig;
        switch (type) {
          case pin::Type::ADC:
            if ((sig->features() & IPin::Features::ADC) != 0)
              return new cd74hc4067::ADC(this);
            break;
          default:
            return nullptr;
        }
        return nullptr;
      }

      esp_err_t select() {
        return _owner->selectChannel(_id);
      }

     private:
      CD74HC4067 *_owner;
      char _name[14];
      friend class cd74hc4067::ADC;
    };

    CD74HC4067::CD74HC4067(IPin *pinEn, IPin *pinS0, IPin *pinS1, IPin *pinS2,
                           IPin *pinS3, IPin *pinSig)
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
        ESP_CHECK_RETURN(_en->digitalWrite(!en));
      return ESP_OK;
    }

    IPin *CD74HC4067::newPin(int id) {
      return new CD74HC4067Pin(this, id);
    }

    esp_err_t CD74HC4067::selectChannel(int c) {
      c -= _pinBase;
      if (c < 0 || c > 15)
        return ESP_ERR_INVALID_ARG;
      esp_err_t err = ESP_OK;
      pin::Tx tx(pin::Tx::Type::Write, &err);
      ESP_CHECK_RETURN(_s0->digitalWrite((c & 0x1) != 0));
      ESP_CHECK_RETURN(_s1->digitalWrite((c & 0x2) != 0));
      ESP_CHECK_RETURN(_s2->digitalWrite((c & 0x4) != 0));
      ESP_CHECK_RETURN(_s3->digitalWrite((c & 0x8) != 0));
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
      return new CD74HC4067(pinEn, pinS0, pinS1, pinS2, pinS3, pinSig);
    }

  }  // namespace io
}  // namespace esp32m