#include "esp32m/dev/maqi136.hpp"
#include <math.h>

#include <esp_task_wdt.h>

#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {
    namespace {
      // Conventional metre of water: 1 mH2O = 9806.65 Pa.
      constexpr float PaPerMeterH2O = 9806.65f;
      constexpr float PaPerPsi = 6894.757293168f;
      constexpr uint32_t BaudRates[] = {1200, 2400, 4800, 9600, 19200};
      // Shared-bus trace: the sensor answers after startup idle, but remains
      // silent when its request follows another slave's reply by 10-30 ms.
      // Reserve this compatibility trial inside the UART lock; a delay here
      // would allow another task to use the bus during the intended gap.
      constexpr uint32_t QuietBeforeMs = 100;

      esp_err_t requestOnce(uint8_t address, modbus::Command command,
                            uint16_t reg, uint16_t count, void *data) {
        const auto error = modbus::Master::instance().request(
            address, command, reg, count, data, false, QuietBeforeMs);
        if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
        return error;
      }
    }  // namespace

    Maqi136::Maqi136(uint8_t addr, uint32_t baud)
        : _addr(addr), _baud(baud), _sensor(this, "distance", "level") {
      _sensor.unit = "m";
      _sensor.precision = _precision;
      _sensor.stateClass = StateClass::Measurement;
      Device::init(Flags::HasSensors | Flags::CanDisable);
    }

    void Maqi136::calibrate(float multiplier, float offset) {
      _calibrationMultiplier = multiplier;
      _calibrationOffset = offset;
      _calibrated = true;
    }

    void Maqi136::setRange(float min, float max) {
      _rangeMin = min;
      _rangeMax = max;
      _rangeOverridden = true;
    }

    bool Maqi136::metersPerUnit(Units units, float &factor) {
      switch (units) {
        case Units::MPa:
          factor = 1000000.0f / PaPerMeterH2O;
          return true;
        case Units::KPa:
          factor = 1000.0f / PaPerMeterH2O;
          return true;
        case Units::Pa:
          factor = 1.0f / PaPerMeterH2O;
          return true;
        case Units::Bar:
          factor = 100000.0f / PaPerMeterH2O;
          return true;
        case Units::Mbar:
          factor = 100.0f / PaPerMeterH2O;
          return true;
        case Units::Psi:
          factor = PaPerPsi / PaPerMeterH2O;
          return true;
        case Units::MH2O:
          factor = 1.0f;
          return true;
        case Units::MmH2O:
          factor = 0.001f;
          return true;
        default:
          return false;
      }
    }

    uint8_t Maqi136::precisionForStep(float step) {
      step = fabsf(step);
      uint8_t precision = 0;
      while (step > 0 && step < 1 && precision < 6) {
        step *= 10;
        precision++;
      }
      return precision;
    }

    int Maqi136::baudCode(uint32_t baud) {
      for (size_t i = 0; i < std::size(BaudRates); ++i)
        if (BaudRates[i] == baud) return static_cast<int>(i);
      return -1;
    }

    bool Maqi136::validAddress(unsigned address) {
      return address >= 1 && address <= 247;
    }

    void Maqi136::invalidate() {
      _stamp = -1;
      _sensor.resetState();
    }

    esp_err_t Maqi136::prepareBus(uint32_t baud) {
      auto &mb = modbus::Master::instance();
      if (!mb.isConfigured() || mb.config().ser_opts.mode != MB_RTU ||
          mb.config().ser_opts.parity != UART_PARITY_DISABLE)
        return ESP_ERR_INVALID_STATE;
      ESP_CHECK_RETURN(mb.setBaudrate(baud));
      return mb.isRunning() ? ESP_OK : mb.start();
    }

    esp_err_t Maqi136::ready() {
      auto &mb = modbus::Master::instance();
      if (_pending || !validAddress(_addr) || baudCode(_baud) < 0 ||
          !mb.isConfigured() || mb.config().ser_opts.mode != MB_RTU ||
          mb.config().ser_opts.parity != UART_PARITY_DISABLE)
        return ESP_ERR_INVALID_STATE;
      if (_restoreBus) {
        ESP_CHECK_RETURN(prepareBus(_baud));
        _restoreBus = false;
      }
      if (mb.config().ser_opts.baudrate != _baud)
        return ESP_ERR_INVALID_STATE;
      return mb.isRunning() ? ESP_OK : mb.start();
    }

    esp_err_t Maqi136::probe(uint8_t address, uint32_t baud,
                             JsonObject details) {
      if (!details.isNull()) {
        details["address"] = address;
        details["baud"] = baud;
      }
      auto read = [&]() -> esp_err_t {
        ESP_CHECK_RETURN(prepareBus(baud));
        uint16_t communication[2] = {};
        ESP_CHECK_RETURN(requestOnce(address, modbus::Command::ReadHolding,
                                     AddressRegister, 2, communication));
        if (!details.isNull()) {
          details["addressRaw"] = communication[0];
          details["baudRaw"] = communication[1];
        }
        return communication[0] == address &&
                       communication[1] == baudCode(baud)
                   ? ESP_OK
                   : ESP_ERR_INVALID_RESPONSE;
      };
      const auto error = read();
      if (!details.isNull()) details["error"] = error;
      return error;
    }

    esp_err_t Maqi136::resolvePending() {
      if (!_pending) return ready();
      if (probe(_pendingAddress, _pendingBaud) == ESP_OK) {
        _addr = _pendingAddress;
        _baud = _pendingBaud;
      } else if (probe(_addr, _baud) != ESP_OK) {
        const auto restoreError = prepareBus(_baud);
        return restoreError != ESP_OK ? restoreError : ESP_ERR_INVALID_STATE;
      }
      _pending = _restoreBus = false;
      invalidate();
      config::Changed::publish(this);
      return ESP_OK;
    }

    esp_err_t Maqi136::changeCommunication(uint16_t reg, uint16_t raw,
                                            uint8_t address, uint32_t baud,
                                            bool &changed,
                                            JsonObject details) {
      details["address"] = address;
      details["baud"] = baud;
      details["writeAttempted"] = false;
      auto checked = [&](esp_err_t error, const char *stage) {
        details["stage"] = stage;
        return error;
      };
      if (_pending) {
        ESP_CHECK_RETURN(checked(
            resolvePending(),
            "Previous communication change is unresolved; no new write attempted"));
        changed = true;
      }
      if (address == _addr && baud == _baud) return ESP_OK;
      ESP_CHECK_RETURN(checked(
          probe(_addr, _baud, details["before"].to<JsonObject>()),
          "Current R0/R1 settings could not be verified; no write attempted"));

      // A clean timeout at the destination tuple is the only safe evidence
      // that another slave is not already using it.
      ESP_CHECK_RETURN(checked(
          prepareBus(baud),
          "Could not prepare destination baud rate; no write attempted"));
      uint16_t occupied[2] = {};
      const auto occupancyError = requestOnce(
          address, modbus::Command::ReadHolding, AddressRegister, 2, occupied);
      details["destinationProbeError"] = occupancyError;
      if (occupancyError == ESP_OK) {
        details["destinationAddressRaw"] = occupied[0];
        details["destinationBaudRaw"] = occupied[1];
      }
      ESP_CHECK_RETURN(checked(
          prepareBus(_baud),
          "Could not restore current baud rate; no write attempted"));
      if (occupancyError != ESP_ERR_TIMEOUT)
        return checked(
            occupancyError == ESP_OK ? ESP_ERR_INVALID_STATE : occupancyError,
            occupancyError == ESP_OK
                ? "Destination already responds; no write attempted"
                : "Destination probe failed without a clean timeout; no write attempted");

      _pendingAddress = address;
      _pendingBaud = baud;
      _pending = true;
      changed = true;
      details["writeAttempted"] = true;
      details["writeRegister"] = reg;
      details["writeRaw"] = raw;
      invalidate();
      const auto writeError = requestOnce(
          _addr, modbus::Command::WriteRegister, reg, 1, &raw);
      details["writeError"] = writeError;
      delay(100);
      if (probe(address, baud, details["after"].to<JsonObject>()) == ESP_OK) {
        _addr = address;
        _baud = baud;
        _pending = false;
        return ESP_OK;
      }
      if (probe(_addr, _baud, details["previous"].to<JsonObject>()) == ESP_OK)
        _pending = false;
      const auto restoreError = prepareBus(_baud);
      if (restoreError != ESP_OK)
        return checked(restoreError, "Could not restore previous bus baud rate");
      return checked(
          writeError == ESP_OK ? ESP_ERR_INVALID_RESPONSE : writeError,
          _pending
              ? "Write outcome unknown; neither communication setting verified; do not repeat the write"
              : "New communication settings not verified; previous settings still respond");
    }

    bool Maqi136::initSensors() {
      std::unique_lock guard(_mutex, std::try_to_lock);
      if (!guard.owns_lock()) return false;
      _lastError = _pending ? resolvePending() : ready();
      if (_lastError != ESP_OK) invalidate();
      return _lastError == ESP_OK;
    }

    JsonDocument *Maqi136::getState(RequestContext &ctx) {
      std::lock_guard guard(_mutex);
      auto regc = sizeof(_regs) / sizeof(uint16_t);
      JsonDocument *doc = new JsonDocument();
      JsonArray arr = doc->to<JsonArray>();
      arr.add(_stamp);
      arr.add(_addr);
      arr.add(_units);
      arr.add(_decimals);
      arr.add(_precision);
      arr.add(_min);
      arr.add(_max);
      arr.add(_value);
      arr.add(_offset);
      auto regs = arr.add<JsonArray>();
      for (auto i = 0; i < regc; i++) regs.add(_regs[i]);
      arr.add(_calibrated);
      arr.add(_calibrationMultiplier);
      arr.add(_calibrationOffset);
      arr.add(_baud);
      arr.add(_pending);
      arr.add(_lastError);
      return doc;
    }

    JsonDocument *Maqi136::getDeviceConfig(RequestContext &ctx) {
      std::lock_guard guard(_mutex);
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      root["address"] = _addr;
      root["baud"] = _baud;
      if (_pending) {
        root["pendingAddress"] = _pendingAddress;
        root["pendingBaud"] = _pendingBaud;
      }
      if (!ctx.request.isInternal() && !isDisabled() && !_pending) {
        auto details = root["communication"].to<JsonObject>();
        auto &master = modbus::Master::instance();
        esp_err_t error = ESP_ERR_INVALID_STATE;
        if (!master.isConfigured() || !master.isRunning() ||
            master.config().ser_opts.mode != MB_RTU ||
            master.config().ser_opts.parity != UART_PARITY_DISABLE ||
            master.config().ser_opts.baudrate != _baud) {
          details["address"] = _addr;
          details["baud"] = _baud;
          details["error"] = error;
        } else
          error = probe(_addr, _baud, details);
        root["communicationError"] = error;
        if (_communicationChange)
          root["lastCommunicationChange"].set(*_communicationChange);
      }
      return doc;
    }

    bool Maqi136::setDeviceConfig(RequestContext &ctx) {
      std::lock_guard guard(_mutex);
      const auto data = ctx.data;
      unsigned address = _addr;
      uint32_t baud = _baud;
      if (!data["address"].isUnbound()) {
        if (!data["address"].is<unsigned>()) {
          ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid Modbus address");
          return false;
        }
        address = data["address"].as<unsigned>();
      }
      if (!data["baud"].isUnbound()) {
        if (!data["baud"].is<uint32_t>()) {
          ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid baud rate");
          return false;
        }
        baud = data["baud"].as<uint32_t>();
      }
      if (!validAddress(address) || baudCode(baud) < 0) {
        ctx.errors.check(
            ESP_ERR_INVALID_ARG,
            "Use Modbus address 1..247 and baud 1200, 2400, 4800, 9600, or 19200");
        return false;
      }
      if (ctx.request.isInternal()) {
        const bool pending = !data["pendingAddress"].isUnbound() ||
                             !data["pendingBaud"].isUnbound();
        if (pending &&
            (!data["pendingAddress"].is<unsigned>() ||
             !validAddress(data["pendingAddress"].as<unsigned>()) ||
             !data["pendingBaud"].is<uint32_t>() ||
             baudCode(data["pendingBaud"].as<uint32_t>()) < 0)) {
          ctx.errors.check(ESP_ERR_INVALID_ARG,
                           "Invalid pending communication settings");
          return false;
        }
        const bool changed = _addr != address || _baud != baud ||
                             _pending != pending;
        _addr = address;
        _baud = baud;
        _pending = pending;
        _pendingAddress = data["pendingAddress"].as<uint8_t>();
        _pendingBaud = data["pendingBaud"].as<uint32_t>();
        _restoreBus = true;
        auto &mb = modbus::Master::instance();
        if (mb.isConfigured()) {
          const auto previousBaud = mb.config().ser_opts.baudrate;
          const auto error = mb.setBaudrate(baud);
          _lastError = error;
          ctx.errors.check(error, "Cannot restore the configured Modbus baud rate");
          if (error != ESP_OK)
            ctx.errors.check(mb.setBaudrate(previousBaud),
                             "Cannot restore the previous Modbus baud rate");
          else
            _restoreBus = false;
        }
        invalidate();
        resetSensors(true);
        return changed;
      }
      if (ctx.request.isBroadcast() || _pending) {
        ctx.errors.check(
            ESP_ERR_INVALID_STATE,
            _pending
                ? "Previous communication change is unresolved; wait for read-only recovery"
                : "Target the MAQI-136 device explicitly");
        return false;
      }

      bool changed = false;
      _communicationChange = std::make_unique<JsonDocument>();
      auto details = _communicationChange->to<JsonObject>();
      auto apply = [&](esp_err_t error, const char *message) {
        _lastError = error;
        ctx.errors.check(error, "%s (%s, %d)", message,
                         esp_err_to_name(error), int(error));
        return error == ESP_OK;
      };
      if (address != _addr &&
          !apply(changeCommunication(AddressRegister, address, address, _baud,
                                     changed,
                                     details["addressChange"].to<JsonObject>()),
                 "Address change not verified"))
        return changed;
      if (baud != _baud &&
          !apply(changeCommunication(BaudRegister, baudCode(baud), _addr, baud,
                                     changed,
                                     details["baudChange"].to<JsonObject>()),
                 "Baud-rate change not verified"))
        return changed;
      if (changed) {
        _lastError = ESP_OK;
        invalidate();
        resetSensors(true);
      }
      return changed;
    }

    bool Maqi136::pollSensors() {
      std::unique_lock guard(_mutex, std::try_to_lock);
      if (!guard.owns_lock()) return true;
      _lastError = _pending ? resolvePending() : ready();
      if (_lastError != ESP_OK) {
        invalidate();
        return false;
      }
      auto err = requestOnce(_addr, modbus::Command::ReadHolding, 0,
                             sizeof(_regs) / sizeof(uint16_t), &_regs);
      if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        _lastError = err;
        invalidate();
        return false;
      }

      if (_regs[AddressRegister] != _addr ||
          _regs[BaudRegister] != baudCode(_baud)) {
        logW("communication metadata mismatch: R0=%u R1=%u expected=%u/%d",
             unsigned(_regs[AddressRegister]), unsigned(_regs[BaudRegister]),
             unsigned(_addr), baudCode(_baud));
        _lastError = ESP_ERR_INVALID_RESPONSE;
        invalidate();
        return false;
      }

      auto units = static_cast<Units>(_regs[2]);
      auto decimals = _regs[3];
      float factor;
      if (decimals > 3) {
        logW("invalid decimal count: %u", (unsigned)decimals);
        invalidate();
        return false;
      }
      if (!metersPerUnit(units, factor)) {
        logW("unit code %u cannot be converted to water level",
             (unsigned)_regs[2]);
        invalidate();
        return false;
      }
      auto rawMeasurement = static_cast<int16_t>(_regs[4]);
      if (rawMeasurement < 0) {
        logW("negative calibrated measurement: %d", (int)rawMeasurement);
        invalidate();
        return false;
      }

      constexpr float divisors[] = {1.0f, 10.0f, 100.0f, 1000.0f};
      auto divisor = divisors[decimals];
      auto toMeters = [divisor, factor](uint16_t raw) {
        return static_cast<int16_t>(raw) / divisor * factor;
      };
      _units = _regs[2];
      _decimals = decimals;
      _precision = precisionForStep(
          _calibrated ? _calibrationMultiplier : factor / divisor);
      if (_rangeOverridden) {
        _min = _rangeMin;
        _max = _rangeMax;
      } else {
        _min = toMeters(_regs[5]);
        _max = toMeters(_regs[6]);
      }
      if (_calibrated) {
        _value = rawMeasurement > 0
                     ? rawMeasurement * _calibrationMultiplier +
                           _calibrationOffset
                     : 0.0f;
        _offset = static_cast<int16_t>(_regs[7]) * _calibrationMultiplier;
      } else {
        _value = toMeters(_regs[4]);
        _offset = toMeters(_regs[7]);
      }
      _stamp = esp_timer_get_time() / 1000LL;
      _lastError = ESP_OK;
      _sensor.precision = _precision;
      _sensor.set(_value);
      return true;
    }

    Maqi136 *useMaqi136(uint8_t addr, uint32_t baud) {
      return new Maqi136(addr, baud);
    }

  }  // namespace dev
}  // namespace esp32m
