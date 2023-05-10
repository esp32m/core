#include "esp32m/io/pins.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/io/softpwm.hpp"

#include <map>
#include <mutex>

namespace esp32m {
  namespace io {
    namespace pwm {
      class Pin : public pin::IPWM {
       public:
        Pin(pin::IDigital *digital) : _digital(digital) {
          _pwm = std::unique_ptr<SoftPwm>(
              new SoftPwm([this](bool high) { _digital->write(high); }));
        };
        esp_err_t setDuty(float value) override {
          return _pwm->setDuty(value);
        }
        float getDuty() const override {
          return _pwm->getDuty();
        }
        esp_err_t setFreq(uint32_t value) override {
          return _pwm->setFreq(value);
        }
        uint32_t getFreq() const override {
          return _pwm->getFreq();
        }
        esp_err_t enable(bool on) override {
          if (on)
            _digital->setDirection(false, true);
          return _pwm->enable(on);
        }
        bool isEnabled() const override {
          return _pwm->isEnabled();
        }

       private:
        pin::IDigital *_digital;
        std::unique_ptr<SoftPwm> _pwm;
      };
    }  // namespace pwm

    namespace pins {
      std::mutex mutex;
      std::map<std::string, IPins *> providers;
      std::map<std::string, IPins *> getProviders() {
        return providers;
      }
      IPin *pin(const char *provider, int id) {
        if (!provider)
          return nullptr;
        std::lock_guard guard(mutex);
        auto p = providers.find(provider);
        if (p == providers.end())
          return nullptr;
        return p->second->pin(id);
      }
    }  // namespace pins

    void IPins::init(int count) {
      _count = count;
      std::lock_guard guard(pins::mutex);
      pins::providers[name()] = this;
    }

    IPin *IPins::pin(int id) {
      if (id < 0 || id >= _count)
        return nullptr;
      std::lock_guard guard(_mutex);
      auto pin = _pins.find(id);
      if (pin == _pins.end()) {
        auto p = newPin(id);
        // p may be nullptr !
        _pins[id] = p;
        return p;
      }
      return pin->second;
    }

    void IPin::reset() {
      std::lock_guard guard(_mutex);
      _features.clear();
    }

    esp_err_t IPin::createFeature(pin::Type type, pin::Feature **feature) {
      switch (type) {
        case pin::Type::PWM:
          if ((flags() & pin::Flags::Output) != 0) {
            auto dig = digital();
            if (dig) {
              *feature = new pwm::Pin(dig);
              return ESP_OK;
            }
          }
          break;
        default:
          break;
      }
      *feature = nullptr;
      return ESP_ERR_NOT_FOUND;
    }

    pin::Feature *IPin::feature(pin::Type type) {
      {
        std::lock_guard guard(_mutex);
        auto it = _features.find(type);
        if (it != _features.end())
          return it->second.get();
      }
      // make sure createFeature is not wrappend in mutex - it may be re-entrant
      pin::Feature *result = nullptr;
      if (createFeature(type, &result) == ESP_OK) {
        std::lock_guard guard(_mutex);
        _features[type] = std::unique_ptr<pin::Feature>(result);
      }
      return result;
    }

    esp_err_t pin::IADC::read(float &value, uint32_t *mv) {
      int raw;
      ESP_CHECK_RETURN(read(raw, mv));
      auto width = getWidth();
      if (width <= 0)
        return ESP_FAIL;
      value = (float)raw / (1 << width);
      return ESP_OK;
    }

    namespace pin {
      int Tx::_nesting = 0;
      Tx *Tx::_current = nullptr;
      Tx::Tx(Type type, esp_err_t *errPtr) : _type(type), _errPtr(errPtr) {
        _mutex.lock();
        if (!_current)
          _current = this;
        _nesting++;
      }
      Tx::~Tx() {
        _nesting--;
        if (_nesting == 0)
          _current = nullptr;
        if (_finalizer) {
          auto err = _finalizer->commit();
          if (err) {
            if (_errPtr)
              *_errPtr = err;
            else
              ESP_ERROR_CHECK_WITHOUT_ABORT(err);
          }
        }
        _mutex.unlock();
      }
      esp_err_t Tx::setFinalizer(ITxFinalizer *finalizer) {
        if (_finalizer)
          return ESP_FAIL;
        _finalizer = finalizer;
        return ESP_OK;
      }

    }  // namespace pin

  }  // namespace io

}  // namespace esp32m