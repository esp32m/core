#include "esp32m/io/pins.hpp"
#include "esp32m/defs.hpp"

#include <map>
#include <mutex>

namespace esp32m {
  namespace io {
    std::map<int, IPin *> IPins::_pins;
    std::mutex IPins::_pinsMutex;
    int IPins::_reservedIds = 0;

    void IPins::init(int pinCount) {
      _pinCount = pinCount;
      std::lock_guard guard(_pinsMutex);
      _pinBase = _reservedIds + 1;
      _reservedIds += pinCount;
    }

    int IPins::id2num(int id) {
      auto p = id - _pinBase;
      if (p < 0 || p >= _pinCount)
        return -1;
      return p;
    }

    esp_err_t IPins::add(IPin *pin) {
      if (!pin)
        return ESP_ERR_INVALID_ARG;
      auto id = pin->id();
      if (_pins.find(id) != _pins.end())
        return ESP_FAIL;
      _pins[id] = pin;
      return ESP_OK;
    }

    IPin *IPins::pin(int num) {
      if (num < 0 || num >= _pinCount)
        return nullptr;
      auto id = num + _pinBase;
      std::lock_guard guard(_pinsMutex);
      auto pin = _pins.find(id);
      if (pin == _pins.end()) {
        auto p = newPin(id);
        _pins[id] = p;
        return p;
      }
      return pin->second;
    }

    void IPin::reset() {
      std::lock_guard guard(_mutex);
      _impls.clear();
    }
    pin::Impl *IPin::impl(pin::Type type) {
      std::lock_guard guard(_mutex);
      pin::Impl *result = nullptr;
      auto it = _impls.find(type);
      if (it == _impls.end()) {
        auto i = createImpl(type);
        if (i) {
          if (i->valid())
            _impls[type] = std::unique_ptr<pin::Impl>(result = i);
          else
            delete i;
        }
      } else
        result = it->second.get();
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