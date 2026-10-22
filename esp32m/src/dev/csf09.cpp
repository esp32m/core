#include "esp32m/dev/csf09.hpp"

#include <esp_timer.h>
#include <esp_task_wdt.h>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m::dev {
  namespace {
    uint64_t nowMs() { return esp_timer_get_time() / 1000ULL; }

    bool busReady() {
      const auto& mb = modbus::Master::instance();
      return mb.isConfigured() && mb.isRunning() &&
             mb.config().ser_opts.mode == MB_RTU &&
             mb.config().ser_opts.parity == UART_PARITY_DISABLE &&
             mb.config().ser_opts.baudrate == 9600;
    }

    esp_err_t requestOnce(uint8_t address, modbus::Command command,
                          uint16_t reg, uint16_t count, void* data) {
      if (!busReady()) return ESP_ERR_INVALID_STATE;
      const auto error = modbus::Master::instance().request(
          address, command, reg, count, data, false);
      // Pending recovery can run in the watched shared sensor task. Each
      // completed bounded transaction represents progress, even on timeout.
      if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
      return error;
    }

    esp_err_t requestOnce(uint8_t address, modbus::Command command,
                          uint16_t reg, uint16_t& value) {
      return requestOnce(address, command, reg, 1, &value);
    }
  }  // namespace

  Csf09::Csf09(uint8_t addr, const char* name)
      : _name(name && *name ? name : "CSF09"),
        _addr(addr),
        _moisture(this, "moisture"),
        _temperature(this, "temperature") {
    _moisture.precision = 1;
    _moisture.unit = "%";
    _moisture.stateClass = StateClass::Measurement;
    _temperature.precision = 1;
    _temperature.unit = "°C";
    _temperature.stateClass = StateClass::Measurement;
    Device::init(Flags::HasSensors | Flags::CanDisable);
  }

  bool Csf09::validAddress(unsigned address) {
    return address >= 1 && address <= 247;
  }

  void Csf09::invalidate() {
    _seen = false;
    _moisture.resetState();
    _temperature.resetState();
  }

  esp_err_t Csf09::ready() {
    auto& mb = modbus::Master::instance();
    if (!validAddress(_addr) || !mb.isConfigured() ||
        mb.config().ser_opts.mode != MB_RTU ||
        mb.config().ser_opts.parity != UART_PARITY_DISABLE ||
        mb.config().ser_opts.baudrate != 9600)
      return ESP_ERR_INVALID_STATE;
    return mb.isRunning() ? ESP_OK : mb.start();
  }

  esp_err_t Csf09::probe(uint8_t address) {
    if (!busReady()) return ESP_ERR_INVALID_STATE;
    // Only the measurement read is documented for ordinary slave addresses.
    // The address-register read is shown solely as isolated 0xFF discovery.
    // The RTU transport validates the responding slave ID and function/count.
    uint16_t values[2] = {};
    const auto error = requestOnce(address, modbus::Command::ReadHolding,
                                   0x0000, 2, values);
    return error != ESP_OK ? error
                          : values[0] <= 1000 && values[1] <= 1000
                                ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
  }

  esp_err_t Csf09::resolvePending() {
    if (!_pendingAddress) return ESP_OK;
    if (probe(_pendingAddress) == ESP_OK)
      _addr = _pendingAddress;
    else if (probe(_addr) != ESP_OK)
      return ESP_ERR_INVALID_STATE;
    _pendingAddress = 0;
    invalidate();
    config::Changed::publish(this);
    return ESP_OK;
  }

  bool Csf09::initSensors() {
    std::unique_lock guard(_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return false;
    _lastError = ready();
    if (_lastError == ESP_OK) _lastError = resolvePending();
    return _lastError == ESP_OK;
  }

  bool Csf09::pollSensors() {
    std::unique_lock guard(_mutex, std::try_to_lock);
    if (!guard.owns_lock()) return true;
    _lastError = ready();
    if (_lastError == ESP_OK) _lastError = resolvePending();
    if (_lastError != ESP_OK) return false;
    auto& mb = modbus::Master::instance();

    uint16_t values[2] = {};
    _lastError = mb.request(_addr, modbus::Command::ReadHolding, 0x0000, 2,
                            values);
    if (_lastError != ESP_OK) return false;
    // Both registers are unsigned 0..1000. Reject the whole sample before
    // changing either sensor or marking the cached reading as fresh.
    if (values[0] > 1000 || values[1] > 1000) {
      _lastError = ESP_ERR_INVALID_RESPONSE;
      return false;
    }

    _moisture.set(values[0] / 10.0f);
    // Temperature is offset binary, not a signed tenths-of-a-degree value.
    _temperature.set(values[1] / 10.0f - 50.0f);
    _stamp = nowMs();
    _seen = true;
    return true;
  }

  JsonDocument* Csf09::getState(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    auto doc = new JsonDocument();
    auto state = doc->to<JsonArray>();
    state.add(_seen ? static_cast<int64_t>(_stamp) : -1);
    state.add(_addr);
    state.add(_moisture.get());
    state.add(_temperature.get());
    state.add(_pendingAddress != 0);
    state.add(_lastError);
    return doc;
  }

  JsonDocument* Csf09::getDeviceConfig(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    esp_err_t error = ESP_OK;
    if (!ctx.request.isInternal() && !isDisabled()) {
      // Settings do not explicitly start or retune the shared master.
      // Normal Modbus transport recovery may restart it after an error.
      error = busReady() && validAddress(_addr) ? ESP_OK : ESP_ERR_INVALID_STATE;
      if (error == ESP_OK)
        error = _pendingAddress ? resolvePending() : probe(_addr);
      _lastError = error;
    }
    auto doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    root["address"] = _addr;
    root["baud"] = 9600;
    if (_pendingAddress) root["pendingAddress"] = _pendingAddress;
    if (!ctx.request.isInternal()) root["communicationError"] = error;
    return doc;
  }

  bool Csf09::setDeviceConfig(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    const auto data = ctx.data;
    if (!data["connectAddress"].isUnbound()) {
      // Selecting another existing sensor is a local driver operation. It
      // must not be confused with the address field, which programs an ID.
      if (ctx.request.isInternal() || ctx.request.isBroadcast() ||
          isDisabled() || _pendingAddress) {
        ctx.errors.check(
            ESP_ERR_INVALID_STATE,
            "Target an enabled CSF09 and resolve any pending ID change before connecting another sensor");
        return false;
      }
      if (data.size() != 1 || !data["connectAddress"].is<unsigned>() ||
          !validAddress(data["connectAddress"].as<unsigned>())) {
        ctx.errors.check(
            ESP_ERR_INVALID_ARG,
            "Send connectAddress alone as an integer from 1 to 247");
        return false;
      }
      const auto selected = data["connectAddress"].as<uint8_t>();
      _lastError = probe(selected);
      if (_lastError != ESP_OK) {
        ctx.errors.check(_lastError,
                         "No valid CSF09 reading at the selected ID; the polling address was not changed");
        return false;
      }
      if (selected == _addr) return false;
      _addr = selected;
      invalidate();
      resetSensors(true);
      // Normal config persistence saves address, not this one-off command.
      return true;
    }
    unsigned address = _addr;
    if (!data["address"].isUnbound()) {
      if (!data["address"].is<unsigned>() ||
          !validAddress(data["address"].as<unsigned>())) {
        ctx.errors.check(ESP_ERR_INVALID_ARG, "Use Modbus address 1..247");
        return false;
      }
      address = data["address"].as<unsigned>();
    }
    if (!validAddress(address)) {
      ctx.errors.check(ESP_ERR_INVALID_ARG, "Use Modbus address 1..247");
      return false;
    }
    if (!data["baud"].isUnbound() &&
        (!data["baud"].is<uint32_t>() || data["baud"].as<uint32_t>() != 9600)) {
      ctx.errors.check(ESP_ERR_INVALID_ARG,
                       "CSF09 baud-rate changes are not documented; use 9600");
      return false;
    }
    if (ctx.request.isInternal()) {
      unsigned pending = 0;
      if (!data["pendingAddress"].isUnbound()) {
        if (!data["pendingAddress"].is<unsigned>() ||
            !validAddress(data["pendingAddress"].as<unsigned>()) ||
            data["pendingAddress"].as<unsigned>() == address) {
          ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid pending Modbus address");
          return false;
        }
        pending = data["pendingAddress"].as<unsigned>();
      }
      const bool changed = _addr != address || _pendingAddress != pending;
      _addr = address;
      _pendingAddress = pending;
      _lastError = ESP_OK;
      invalidate();
      resetSensors(true);
      return changed;
    }
    if (ctx.request.isBroadcast() || isDisabled() || _pendingAddress ||
        !data["pendingAddress"].isUnbound()) {
      ctx.errors.check(ESP_ERR_INVALID_STATE,
                       "Target an enabled CSF09 and resolve any pending ID change first");
      return false;
    }
    if (address == _addr) return false;
    auto fail = [&](esp_err_t error, const char* message) {
      _lastError = error;
      ctx.errors.check(error, "%s", message);
    };
    auto error = probe(_addr);
    if (error != ESP_OK) {
      fail(error, "Current Modbus ID could not be verified; no write attempted");
      return false;
    }
    error = probe(address);
    if (error != ESP_ERR_TIMEOUT) {
      fail(error == ESP_OK ? ESP_ERR_INVALID_STATE : error,
           "Destination responds or could not be checked; no write attempted");
      return false;
    }

    // Never use the manual's isolated-sensor 0xFF discovery on a shared bus.
    _pendingAddress = address;
    invalidate();
    resetSensors(true);
    uint16_t raw = address;
    const auto writeError = requestOnce(_addr, modbus::Command::WriteRegister,
                                        AddressRegister, raw);
    // An ACK (or a timeout) cannot establish where the sensor now responds.
    if (probe(address) == ESP_OK) {
      _addr = address;
      _pendingAddress = 0;
      _lastError = ESP_OK;
      return true;
    }
    if (probe(_addr) == ESP_OK) _pendingAddress = 0;
    fail(writeError == ESP_OK ? ESP_ERR_INVALID_RESPONSE : writeError,
         _pendingAddress
             ? "ID change unresolved; recovery will only read old/new IDs; do not repeat the write"
             : "New ID not verified; the previous ID still responds");
    // Persist both a verified change and an uncertain write outcome. Internal
    // restore and later recovery never write the device again.
    return true;
  }

  Csf09* useCsf09(uint8_t addr, const char* name) {
    return new Csf09(addr, name);
  }

}  // namespace esp32m::dev
