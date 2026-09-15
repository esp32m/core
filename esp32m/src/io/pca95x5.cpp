
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
          return _owner->pinFlags();
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
        pin::Mode getMode() const override {
          std::lock_guard guard(pin::Tx::mutex());
          return _mode;
        }
        esp_err_t setMode(pin::Mode mode) override {
          std::lock_guard guard(pin::Tx::mutex());
          mode = deduce(mode, _pin->flags());
          bool input = ((mode & pin::Mode::Output) == 0);
          if (!input && (_pin->flags() & pin::Flags::OpenDrain) != 0)
            mode |= pin::Mode::OpenDrain;
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

    Pca95x5::Pca95x5(i2c::MasterDev *i2c, pca95x5::Bits bits,
                     pca95x5::Model model)
        : _i2c(i2c), _bits(bits), _model(model) {
      IPins::init(_bits == pca95x5::Bits::Eight ? 8 : 16);
      if (_i2c) {
        _i2c->setEndianness(Endian::Little);
        // Register reads and full latch writes are safe to repeat after NACK.
        _i2c->setRetryInvalidResponse(true);
      }
      std::lock_guard guard(pin::Tx::mutex());
      ESP_ERROR_CHECK_WITHOUT_ABORT(init());
    }

    uint16_t Pca95x5::mask() const {
      return _bits == pca95x5::Bits::Eight ? 0xFF : 0xFFFF;
    }

    bool Pca95x5::validPin(int pin) const {
      return pin >= 0 && pin < count();
    }

    pin::Flags Pca95x5::pinFlags() const {
      auto flags = pin::Flags::Input | pin::Flags::Output;
      if (_model == pca95x5::Model::Pca9555)
        flags |= pin::Flags::PullUp;
      else if (_model == pca95x5::Model::Pca9535C)
        flags |= pin::Flags::OpenDrain;
      return flags;
    }

    void Pca95x5::setRecoveryOutput(uint16_t pins, uint16_t value) {
      std::lock_guard guard(pin::Tx::mutex());
      _recoveryMask = pins & mask();
      _recoveryOutput = value & _recoveryMask;
    }

    esp_err_t Pca95x5::readRegister(pca95x5::Register reg, uint16_t &value) {
      if (!_i2c)
        return ESP_ERR_INVALID_STATE;
      switch (_bits) {
        case pca95x5::Bits::Eight: {
          uint8_t v8;
          auto result = _i2c->read((uint8_t)reg, v8);
          if (result == ESP_OK) {
            value = v8;
          }
          return result;
        }
        case pca95x5::Bits::Sixteen:
          return _i2c->read((uint8_t)reg << 1, value);
      }
      return ESP_FAIL;
    }
    esp_err_t Pca95x5::writeRegister(pca95x5::Register reg, uint16_t value) {
      if (!_i2c)
        return ESP_ERR_INVALID_STATE;
      esp_err_t result = ESP_ERR_INVALID_ARG;
      switch (_bits) {
        case pca95x5::Bits::Eight:
          result = _i2c->write((uint8_t)reg, (uint8_t)value);
          break;
        case pca95x5::Bits::Sixteen:
          result = _i2c->write((uint8_t)reg << 1, value);
          break;
      }
      if (result != ESP_OK) {
        // A failed transfer may still have changed one or both port bytes.
        _recoveryPending = _initialized;
        _targetOutput = _output;
      }
      return result;
    }

    esp_err_t Pca95x5::read(pca95x5::Register reg, uint16_t &value) {
      std::lock_guard guard(pin::Tx::mutex());
      return readRegister(reg, value);
    }

    esp_err_t Pca95x5::write(pca95x5::Register reg, uint16_t value) {
      std::lock_guard guard(pin::Tx::mutex());
      if (reg == pca95x5::Register::Input)
        return ESP_ERR_INVALID_ARG;
      ESP_CHECK_RETURN(checkState(true));
      value &= mask();
      ESP_CHECK_RETURN(writeRegister(reg, value));
      switch (reg) {
        case pca95x5::Register::Output:
          _output = _targetOutput = value;
          break;
        case pca95x5::Register::Config:
          _config = value;
          break;
        case pca95x5::Register::Inversion:
          _inversion = value;
          break;
        default:
          break;
      }
      return ESP_OK;
    }

    esp_err_t Pca95x5::init() {
      uint16_t input, output, config, inversion;
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Input, input));
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Output, output));
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Config, config));
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Inversion, inversion));
      _input = input;
      _output = _targetOutput = output;
      _config = config;
      _inversion = inversion;
      _initialized = true;
      _recoveryPending = false;
      return ESP_OK;
    }

    esp_err_t Pca95x5::recover(uint16_t actualOutput) {
      _recoveryPending = true;
      _targetOutput = _output;
      const uint16_t outputs = ~_config & mask();
      if ((outputs & _recoveryMask) != outputs)
        return ESP_ERR_INVALID_STATE;
      // Never replay a previously running motor. The application supplies safe
      // latch levels, written before any output directions are restored.
      const uint16_t safe = (actualOutput & ~_recoveryMask) | _recoveryOutput;
      ESP_CHECK_RETURN(writeRegister(pca95x5::Register::Output, safe));
      ESP_CHECK_RETURN(writeRegister(pca95x5::Register::Inversion, _inversion));
      ESP_CHECK_RETURN(writeRegister(pca95x5::Register::Config, _config));
      uint16_t config, output, inversion;
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Config, config));
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Output, output));
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Inversion, inversion));
      if (config != _config || output != safe || inversion != _inversion)
        return ESP_ERR_INVALID_RESPONSE;
      _output = _targetOutput = output;
      _recoveryPending = false;
      logW("restored safe output/configuration after register mismatch or failed write");
      return ESP_OK;
    }

    esp_err_t Pca95x5::checkState(bool force) {
      if (!_initialized)
        ESP_CHECK_RETURN(init());
      auto tx = pin::Tx::current();
      if (!force && !_recoveryPending && tx &&
          tx->getReadPerformed(&_verificationKey))
        return ESP_OK;
      uint16_t config, output, inversion;
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Config, config));
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Output, output));
      ESP_CHECK_RETURN(readRegister(pca95x5::Register::Inversion, inversion));
      const bool matches = config == _config && output == _output &&
                           inversion == _inversion;
      const uint16_t outputs = ~_config & mask();
      if (_recoveryPending && matches &&
          (outputs & _recoveryMask) != outputs) {
        // Without an application recovery policy, exact readback can establish
        // that a failed write left the previously confirmed state unchanged.
        // Discard the command and report the interruption once; never replay it.
        _targetOutput = _output;
        _recoveryPending = false;
        return ESP_ERR_INVALID_STATE;
      }
      if (_recoveryPending || !matches) {
        ESP_CHECK_RETURN(recover(output));
        // Report the interrupted operation even after successful recovery.
        // A valve must fault/stop and wait for a new command, not resume motion.
        return ESP_ERR_INVALID_STATE;
      }
      if (tx)
        tx->setReadPerformed(&_verificationKey);
      return ESP_OK;
    }

    IPin *Pca95x5::newPin(int id) {
      return new pca95x5::Pin(this, id);
    }

    esp_err_t Pca95x5::readPin(int pin, bool &value) {
      std::lock_guard guard(pin::Tx::mutex());
      if (!validPin(pin))
        return ESP_ERR_INVALID_ARG;
      ESP_CHECK_RETURN(checkState());
      auto isOutput = (_config & (1 << pin)) == 0;
      if (isOutput) {
        value = _output & (1 << pin);
        return ESP_OK;
      }

      auto tx = pin::Tx::current();
      const bool snapshot = tx && (tx->type() & pin::Tx::Type::Read) != 0;
      if (!snapshot || !tx->getReadPerformed(this)) {
        ESP_CHECK_RETURN(readRegister(pca95x5::Register::Input, _input));
        if (snapshot)
          tx->setReadPerformed(this);
      }
      value = _input & (1 << pin);
      return ESP_OK;
    }
    esp_err_t Pca95x5::writePin(int pin, bool value) {
      std::lock_guard guard(pin::Tx::mutex());
      if (!validPin(pin))
        return ESP_ERR_INVALID_ARG;
      ESP_CHECK_RETURN(checkState());
      auto tx = pin::Tx::current();
      const bool deferred = tx && (tx->type() & pin::Tx::Type::Write) != 0;
      if (deferred)
        ESP_CHECK_RETURN(tx->setFinalizer(this));
      if (value)
        _targetOutput |= (1 << pin);
      else
        _targetOutput &= ~(1 << pin);

      if (!deferred)
        return commit();
      return ESP_OK;
    }
    esp_err_t Pca95x5::setPinMode(int pin, bool input) {
      std::lock_guard guard(pin::Tx::mutex());
      if (!validPin(pin))
        return ESP_ERR_INVALID_ARG;
      ESP_CHECK_RETURN(checkState());
      uint16_t config = _config;
      if (input)
        config |= (1 << pin);
      else
        config &= ~(1 << pin);
      if (config != _config) {
        ESP_CHECK_RETURN(writeRegister(pca95x5::Register::Config, config));
        _config = config;
      }
      return ESP_OK;
    }

    esp_err_t Pca95x5::commit() {
      std::lock_guard guard(pin::Tx::mutex());
      const auto err = checkState(true);
      if (err != ESP_OK) {
        abort();
        return err;
      }
      if (_targetOutput != _output) {
        ESP_CHECK_RETURN(writeRegister(pca95x5::Register::Output, _targetOutput));
        _output = _targetOutput;
      }
      return ESP_OK;
    }

    void Pca95x5::abort() {
      std::lock_guard guard(pin::Tx::mutex());
      _targetOutput = _output;
    }

    Pca95x5 *usePca95x5(uint8_t addr, pca95x5::Model model) {
      return new Pca95x5(i2c::MasterDev::create(addr),
                         pca95x5::Bits::Sixteen, model);
    }

  }  // namespace io
}  // namespace esp32m
