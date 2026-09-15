#include "esp32m/io/pins.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/io/softpwm.hpp"

#include <map>
#include <mutex>
#include <algorithm>

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
      thread_local Tx *Tx::_current = nullptr;

      std::recursive_mutex &Tx::mutex() {
        static std::recursive_mutex mutex;
        return mutex;
      }

      Tx::Tx(Type type, esp_err_t *errPtr)
          : _lock(mutex()),
            _type(type),
            _errPtr(errPtr),
            _previous(_current),
            _root(_previous ? _previous->_root : this) {
        if (_previous)
          _type = static_cast<Type>(_type | _previous->_type);
        _current = this;
      }

      Tx::~Tx() {
        // Only the outer scope owns implicit commit. Never leave a pointer to
        // a destroyed inner scope, even when tasks overlap on different cores.
        if (_root == this && !_aborted) {
          auto err = commit();
          if (err && !_errPtr)
            ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        }
        if (_errPtr && _root->_error)
          *_errPtr = _root->_error;
        _current = _previous;
      }

      const ITxFinalizer *Tx::getFinalizer() const {
        return _root->_finalizers.empty() ? nullptr : _root->_finalizers.front();
      }

      bool Tx::hasFinalizer(const ITxFinalizer *finalizer) const {
        const auto &items = _root->_finalizers;
        return std::find(items.begin(), items.end(), finalizer) != items.end();
      }

      esp_err_t Tx::setFinalizer(ITxFinalizer *finalizer) {
        if (!finalizer)
          return ESP_ERR_INVALID_ARG;
        if (_root->_aborted)
          return _root->_error;
        if (!hasFinalizer(finalizer))
          _root->_finalizers.push_back(finalizer);
        return ESP_OK;
      }

      void Tx::setReadPerformed(const void *provider) {
        if (!getReadPerformed(provider))
          _root->_reads.push_back(provider);
      }

      bool Tx::getReadPerformed(const void *provider) const {
        const auto &items = _root->_reads;
        return std::find(items.begin(), items.end(), provider) != items.end();
      }

      esp_err_t Tx::commit() {
        if (_root->_aborted)
          return _root->_error;
        auto finalizers = std::move(_root->_finalizers);
        _root->_finalizers.clear();
        _root->_reads.clear();
        for (size_t i = 0; i < finalizers.size(); i++) {
          auto err = finalizers[i]->commit();
          if (err) {
            _root->_error = err;
            _root->_aborted = true;
            // A failed commit may have changed hardware partially; rollback
            // only software staging and never replay it in a later command.
            for (; i < finalizers.size(); i++)
              finalizers[i]->abort();
            _root->_reads.clear();
            if (_errPtr)
              *_errPtr = err;
            return err;
          }
        }
        _root->_reads.clear();
        return ESP_OK;
      }

      void Tx::abort() {
        for (auto finalizer : _root->_finalizers)
          finalizer->abort();
        _root->_finalizers.clear();
        _root->_reads.clear();
        _root->_aborted = true;
        if (!_root->_error)
          _root->_error = ESP_ERR_INVALID_STATE;
      }

      PcntSampler::PcntSampler(IPcnt *pcnt) : _pcnt(pcnt) {
        esp_timer_create_args_t args = {
            .callback = [](void *self) { ((PcntSampler*)self)->cb(); },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "pcntsmpl",
            .skip_unhandled_events = true,
        };
        esp_timer_create(&args, &_timer);
      }
      PcntSampler::~PcntSampler() {
        if (_timer) {
          enable(false);
          esp_timer_delete(_timer);
          _timer = nullptr;
        }
      }
      esp_err_t PcntSampler::setPeriod(uint64_t period) {
        if (!_timer)
          return ESP_ERR_INVALID_STATE;
        if (_period) {
          esp_timer_stop(_timer);
          _period = 0;
        }
        _value = 0;
        _freq = 0;
        _ticks = 0;
        if (period) {
          ESP_CHECK_RETURN(esp_timer_start_periodic(_timer, period));
          int value;
          if (_pcnt->read(value) == ESP_OK)
            _value = value;
          _period = period;
          _ticks = esp_timer_get_time();
        }
        return ESP_OK;
      }

      void PcntSampler::cb() {
        if (!_pcnt->isEnabled())
          return;
        int value;
        if (_pcnt->read(value) == ESP_OK) {
          float ppp = value > _value ? value - _value : _value - value;
          auto ticks = esp_timer_get_time();
          auto elapsed = ticks > _ticks ? ticks - _ticks : _ticks - ticks;
          _value = value;
          _ticks = ticks;
          _freq = (_freq + ppp * _period / elapsed) / 2;
        }
      }

    }  // namespace pin

  }  // namespace io

}  // namespace esp32m
