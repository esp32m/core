#include "esp32m/dev/hsmwt10.hpp"

#include <esp_task_wdt.h>
#include <esp_timer.h>

#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"

namespace esp32m::dev {
  namespace {
    constexpr uint32_t BaudRates[] = {2400, 4800, 9600, 19200, 38400, 115200};
    constexpr uint64_t FreshMs = 5000;
    // Compatibility guard for the reported shared-bus timeout: standalone
    // and on-demand reads work, but polling immediately after another slave
    // fails. 100 ms is a conservative trial value, not a vendor specification.
    constexpr uint32_t QuietBeforeMs = 100;

    uint64_t nowMs() { return esp_timer_get_time() / 1000ULL; }

    int baudCode(uint32_t baud) {
      for (unsigned i = 0; i < sizeof(BaudRates) / sizeof(BaudRates[0]); ++i)
        if (BaudRates[i] == baud) return i + 1;
      return 0;
    }

    bool validAddress(unsigned address) {
      // Manual: 1..253. esp-modbus supports only standard RTU IDs 1..247.
      return address >= 1 && address <= 247;
    }

    esp_err_t requestOnce(uint8_t address, modbus::Command command,
                          uint16_t reg, uint16_t& value) {
      const auto error = modbus::Master::instance().request(
          address, command, reg, 1, &value, false, QuietBeforeMs);
      // Pending recovery can require several bounded transactions in the
      // watched sensor task. Each completed transaction represents progress.
      if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
      return error;
    }
  }  // namespace

  HsmWt10::HsmWt10(uint8_t address, uint32_t baud, const char* name)
      : _name(name && *name ? name : "HSM-WT10"),
        _address(address),
        _baud(baud),
        _leak(this, "moisture", "leak"),
        _analogSensor(this, "", "analog") {
    _leak.setTitle("Water leak");
    _analogSensor.setTitle("Raw sensing value");
    _analogSensor.precision = 0;
    _analogSensor.stateClass = StateClass::Measurement;
    Device::init(Flags::HasSensors | Flags::CanDisable);
  }

  bool HsmWt10::fresh() const {
    return _valid && nowMs() - _stamp <= FreshMs;
  }

  bool HsmWt10::isValid() const {
    std::lock_guard guard(_mutex);
    return fresh();
  }

  bool HsmWt10::value() const {
    std::lock_guard guard(_mutex);
    return _alarm;
  }

  void HsmWt10::invalidate() {
    _valid = false;
    _leak.resetState();
    _analogSensor.resetState();
  }

  esp_err_t HsmWt10::ready() {
    auto& mb = modbus::Master::instance();
    if (_pending || !validAddress(_address) || !baudCode(_baud) ||
        !mb.isConfigured() || mb.config().ser_opts.mode != MB_RTU ||
        mb.config().ser_opts.parity != UART_PARITY_DISABLE)
      return ESP_ERR_INVALID_STATE;
    if (_restoreBus) {
      ESP_CHECK_RETURN(prepareBus(_baud));
      _restoreBus = false;
    }
    if (mb.config().ser_opts.baudrate != _baud) return ESP_ERR_INVALID_STATE;
    return mb.isRunning() ? ESP_OK : mb.start();
  }

  esp_err_t HsmWt10::read(uint16_t reg, uint16_t& value) {
    ESP_CHECK_RETURN(ready());
    const auto error =
        requestOnce(_address, modbus::Command::ReadHolding, reg, value);
    if (error != ESP_OK)
      logW("read failed: address=%u register=0x%04x error=%d quiet=%ums",
           unsigned(_address), unsigned(reg), int(error), unsigned(QuietBeforeMs));
    return error;
  }

  esp_err_t HsmWt10::prepareBus(uint32_t baud) {
    auto& mb = modbus::Master::instance();
    if (!mb.isConfigured() || mb.config().ser_opts.mode != MB_RTU ||
        mb.config().ser_opts.parity != UART_PARITY_DISABLE)
      return ESP_ERR_INVALID_STATE;
    ESP_CHECK_RETURN(mb.setBaudrate(baud));
    return mb.isRunning() ? ESP_OK : mb.start();
  }

  esp_err_t HsmWt10::probe(uint8_t address, uint32_t baud) {
    ESP_CHECK_RETURN(prepareBus(baud));
    uint16_t foundAddress = 0, foundBaud = 0;
    ESP_CHECK_RETURN(requestOnce(address, modbus::Command::ReadHolding,
                                 AddressRegister, foundAddress));
    ESP_CHECK_RETURN(requestOnce(address, modbus::Command::ReadHolding,
                                 BaudRegister, foundBaud));
    return foundAddress == address && foundBaud == baudCode(baud)
               ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
  }

  esp_err_t HsmWt10::resolvePending() {
    if (!_pending) return ready();
    // Recover an uncertain write by reading both tuples, never writing again.
    if (probe(_pendingAddress, _pendingBaud) == ESP_OK) {
      _address = _pendingAddress;
      _baud = _pendingBaud;
    } else if (probe(_address, _baud) != ESP_OK) {
      const auto restoreError = prepareBus(_baud);
      return restoreError != ESP_OK ? restoreError : ESP_ERR_INVALID_STATE;
    }
    _pending = _restoreBus = false;
    invalidate();
    config::Changed::publish(this);
    return ESP_OK;
  }

  bool HsmWt10::initSensors() {
    std::unique_lock guard(_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return false;
    _lastError = resolvePending();
    if (_lastError != ESP_OK) invalidate();
    return _lastError == ESP_OK;
  }

  bool HsmWt10::pollSensors() {
    // Configuration may take multiple timeouts. Do not block every other
    // sensor (or its task watchdog) while another task holds this device.
    std::unique_lock guard(_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return true;
    uint16_t alarm = 0, analog = 0;
    _lastError = resolvePending();
    if (_lastError == ESP_OK) _lastError = read(AlarmRegister, alarm);
    if (_lastError == ESP_OK && alarm > 1)
      _lastError = ESP_ERR_INVALID_RESPONSE;
    if (_lastError == ESP_OK) _lastError = read(AnalogRegister, analog);
    if (_lastError != ESP_OK) {
      invalidate();
      return false;
    }
    _alarm = alarm == 1;
    _analog = analog;
    _stamp = nowMs();
    _seen = _valid = true;
    // The alarm register incorporates the device's alarm delay. Do not
    // synthesize a leak flag by comparing the analog value to sensitivity.
    _leak.set(_alarm);
    _analogSensor.set(_analog);
    return true;
  }

  JsonDocument* HsmWt10::getState(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    auto doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    root["address"] = _address;
    root["baud"] = _baud;
    root["valid"] = fresh();
    root["sampledAtMs"] = _seen ? static_cast<int64_t>(_stamp) : -1;
    root["alarm"] = nullptr;
    root["analog"] = nullptr;
    if (fresh()) {
      root["alarm"] = _alarm;
      root["analog"] = _analog;
    }
    root["communicationPending"] = _pending;
    root["lastError"] = _lastError;
    return doc;
  }

  JsonDocument* HsmWt10::getDeviceConfig(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    auto doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    root["address"] = _address;
    root["baud"] = _baud;
    if (_pending) {
      root["pendingAddress"] = _pendingAddress;
      root["pendingBaud"] = _pendingBaud;
    }
    // Device settings are read on demand, never replayed from ESP persistence.
    if (!ctx.request.isInternal() && !isDisabled()) {
      uint16_t sensitivity = 0, alarmDelay = 0;
      auto error = read(SensitivityRegister, sensitivity);
      if (error == ESP_OK && (sensitivity < 200 || sensitivity > 500))
        error = ESP_ERR_INVALID_RESPONSE;
      if (error == ESP_OK) {
        root["sensitivity"] = sensitivity;
        error = read(AlarmDelayRegister, alarmDelay);
        if (error == ESP_OK) root["alarmDelay"] = alarmDelay;
      }
      root["configReadError"] = error;
    }
    return doc;
  }

  esp_err_t HsmWt10::changeCommunication(uint16_t reg, uint16_t raw,
                                         uint8_t address, uint32_t baud,
                                         bool& changed) {
    ESP_CHECK_RETURN(ready());
    ESP_CHECK_RETURN(probe(_address, _baud));
    // A replying device, exception, or corrupt reply is not an empty address.
    auto error = prepareBus(baud);
    if (error != ESP_OK) {
      const auto restoreError = prepareBus(_baud);
      return restoreError != ESP_OK ? restoreError : error;
    }
    uint16_t occupied = 0;
    error = requestOnce(address, modbus::Command::ReadHolding,
                         AddressRegister, occupied);
    const auto restoreError = prepareBus(_baud);
    if (restoreError != ESP_OK) return restoreError;
    if (error != ESP_ERR_TIMEOUT)
      return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;

    _pendingAddress = address;
    _pendingBaud = baud;
    _pending = true;
    changed = true;  // Also persist an uncertain result for read-only recovery.
    invalidate();
    // The manual's address-write ACK uses the NEW slave address. The master
    // may reject it; only independent reads decide whether migration worked.
    const auto writeError = requestOnce(_address, modbus::Command::WriteRegister,
                                         reg, raw);
    delay(100);  // Allow settings to take effect before read-back.
    if (probe(address, baud) == ESP_OK) {
      _address = address;
      _baud = baud;
      _pending = false;
      return ESP_OK;
    }
    if (probe(_address, _baud) == ESP_OK) _pending = false;
    error = prepareBus(_baud);
    logW("communication change not verified: register=%u write=%d pending=%d restore=%d",
         unsigned(reg), int(writeError), int(_pending), int(error));
    if (error != ESP_OK) return error;
    return writeError != ESP_OK ? writeError : ESP_ERR_INVALID_RESPONSE;
  }

  esp_err_t HsmWt10::changeSetting(uint16_t reg, uint16_t value, bool& changed) {
    uint16_t current = 0;
    ESP_CHECK_RETURN(read(reg, current));
    if (current == value) return ESP_OK;
    const auto writeError = requestOnce(_address, modbus::Command::WriteRegister,
                                         reg, value);
    invalidate();
    uint16_t actual = 0;
    const auto readError = read(reg, actual);
    if (readError == ESP_OK && actual == value) {
      changed = true;
      return ESP_OK;
    }
    return readError != ESP_OK ? readError
                              : (writeError != ESP_OK ? writeError
                                                     : ESP_ERR_INVALID_RESPONSE);
  }

  bool HsmWt10::setDeviceConfig(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    const auto data = ctx.data;
    unsigned address = _address;
    uint32_t baud = _baud;
    const auto addressValue = data["address"];
    const auto baudValue = data["baud"];
    const auto sensitivity = data["sensitivity"];
    const auto alarmDelay = data["alarmDelay"];
    if ((!addressValue.isUnbound() &&
         (!addressValue.is<unsigned>() ||
          !validAddress(addressValue.as<unsigned>()))) ||
        (!baudValue.isUnbound() &&
         (!baudValue.is<uint32_t>() || !baudCode(baudValue.as<uint32_t>()))) ||
        (!sensitivity.isUnbound() &&
         (!sensitivity.is<unsigned>() || sensitivity.as<unsigned>() < 200 ||
          sensitivity.as<unsigned>() > 500)) ||
        (!alarmDelay.isUnbound() &&
         (!alarmDelay.is<unsigned>() || alarmDelay.as<unsigned>() > 65535))) {
      ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid HSM-WT10 configuration");
      return false;
    }
    if (!addressValue.isUnbound()) address = addressValue.as<unsigned>();
    if (!baudValue.isUnbound()) baud = baudValue.as<uint32_t>();
    if (!validAddress(address) || !baudCode(baud)) {
      ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid HSM-WT10 address or baud");
      return false;
    }
    if (ctx.request.isInternal()) {
      const bool pending = !data["pendingAddress"].isUnbound() ||
                           !data["pendingBaud"].isUnbound();
      if (pending && (!data["pendingAddress"].is<unsigned>() ||
          !validAddress(data["pendingAddress"].as<unsigned>()) ||
          !data["pendingBaud"].is<uint32_t>() ||
          !baudCode(data["pendingBaud"].as<uint32_t>()))) {
        ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid pending communication settings");
        return false;
      }
      // Local restore only: never issue FC06, even for persisted device fields.
      // The saved tuple remains authoritative even if UART setup fails. Keep
      // it (and any pending migration) and retry local bus setup during init.
      _address = address;
      _baud = baud;
      _pending = pending;
      _pendingAddress = data["pendingAddress"].as<uint8_t>();
      _pendingBaud = data["pendingBaud"].as<uint32_t>();
      _restoreBus = true;
      auto& mb = modbus::Master::instance();
      if (mb.isConfigured()) {
        const auto previousBaud = mb.config().ser_opts.baudrate;
        const auto error = mb.setBaudrate(baud);
        _lastError = error;
        ctx.errors.check(error, "Cannot restore Modbus baud rate");
        if (error != ESP_OK) {
          // setBaudrate may update its config before controller startup fails.
          ctx.errors.check(mb.setBaudrate(previousBaud),
                           "Cannot restore previous Modbus baud rate");
        } else
          _restoreBus = false;
      }
      invalidate();
      resetSensors(true);
      return true;
    }
    if (ctx.request.isBroadcast() || _pending) {
      ctx.errors.check(ESP_ERR_INVALID_STATE, _pending
          ? "Previous communication change is unresolved; wait for read-only recovery"
          : "Target the HSM-WT10 device explicitly");
      return false;
    }
    bool changed = false;
    auto checked = [&](esp_err_t error, const char* stage) {
      _lastError = error;
      ctx.errors.check(error, "%s (%d)", stage, int(error));
      return error == ESP_OK;
    };
    if (address != _address &&
        !checked(changeCommunication(AddressRegister, address, address, _baud,
                                      changed), "Address change not verified"))
      return changed;
    if (baud != _baud &&
        !checked(changeCommunication(BaudRegister, baudCode(baud), _address,
                                      baud, changed), "Baud change not verified"))
      return changed;
    if (!sensitivity.isUnbound() &&
        !checked(changeSetting(SensitivityRegister, sensitivity.as<uint16_t>(),
                                changed), "Sensitivity change not verified"))
      return changed;
    if (!alarmDelay.isUnbound())
      checked(changeSetting(AlarmDelayRegister, alarmDelay.as<uint16_t>(), changed),
              "Alarm delay change not verified");
    return changed;
  }

  HsmWt10* useHsmWt10(uint8_t address, uint32_t baud, const char* name) {
    return new HsmWt10(address, baud, name);
  }

}  // namespace esp32m::dev
