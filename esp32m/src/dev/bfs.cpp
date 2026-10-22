#include "esp32m/dev/bfs.hpp"

#include <esp_task_wdt.h>
#include <esp_timer.h>

#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/dev/bfs/parameters.hpp"

namespace esp32m::dev {
  namespace {
    constexpr uint32_t BaudRates[] = {2400, 4800, 9600, 19200,
                                      38400, 57600, 115200};
    constexpr uint64_t FreshMs = 5000;
    constexpr uint64_t DialogBudgetMs = 20000;
    struct Channel {
      const char* name;
      uint16_t address;
      double scale;
    };
    constexpr Channel Channels[] = {
        {"frequency", 0x1000, 0.01},
        {"frequencySetpoint", 0x1001, 0.01},
        {"voltage", 0x1002, 1},
        {"current", 0x1004, 0.1},
        {"pressureSetpoint", 0x1008, 0.1},
        {"pressure", 0x1009, 0.1},
        {"temperature", 0x100d, 0.1},
        {"status", 0x100c, 1},
    };

    uint64_t nowMs() { return esp_timer_get_time() / 1000ULL; }

    esp_err_t requestOnce(uint8_t slave, modbus::Command command,
                           uint16_t address, uint16_t& value) {
      const auto error = modbus::Master::instance().request(
          slave, command, address, 1, &value, false, 12);
      // Reserve the inter-request gap under the UART lock. Sleeping after
      // releasing it lets another driver fill the gap while a dialog is active.
      // Polling shares a watched task with other devices. Each completed
      // transaction is progress, including a bounded timeout.
      if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
      return error;
    }

    int baudSelector(uint32_t baud) {
      for (size_t i = 0; i < std::size(BaudRates); ++i)
        if (BaudRates[i] == baud) return static_cast<int>(i);
      return -1;
    }

  }  // namespace

  Bfs::Bfs(uint8_t address, uint32_t baud, const char* name)
      : _name(name && *name ? name : "bfs"),
        _address(address),
        _baud(baud),
        _pressure(this, "pressure", "pressure"),
        _frequency(this, "frequency", "frequency"),
        _voltage(this, "voltage", "voltage"),
        _current(this, "current", "current"),
        _temperature(this, "temperature", "temperature") {
    _pressure.unit = "bar";
    _pressure.precision = 1;
    _frequency.unit = "Hz";
    _frequency.precision = 2;
    _voltage.unit = "V";
    _voltage.precision = 0;
    _current.unit = "A";
    _current.precision = 1;
    _temperature.unit = "\xC2\xB0" "C";
    _temperature.precision = 1;
    _temperature.setTitle("Estimated module temperature");
    for (auto sensor : {&_pressure, &_frequency, &_voltage, &_current,
                        &_temperature})
      sensor->stateClass = StateClass::Measurement;
    setSensorsPollInterval(1000);
    Device::init(Flags::HasSensors | Flags::CanDisable);
  }

  esp_err_t Bfs::ready() {
    auto& master = modbus::Master::instance();
    if (!_address || _address > 247 || baudSelector(_baud) < 0 || _pending ||
        !master.isConfigured() || master.config().ser_opts.mode != MB_RTU ||
        master.config().ser_opts.parity != UART_PARITY_DISABLE ||
        master.config().ser_opts.baudrate != _baud)
      return ESP_ERR_INVALID_STATE;
    return master.isRunning() ? ESP_OK : master.start();
  }

  esp_err_t Bfs::read(uint16_t address, uint16_t& value) {
    ESP_CHECK_RETURN(ready());
    const auto error = requestOnce(_address, modbus::Command::ReadHolding, address, value);
    if (error == ESP_OK) cacheParameter(address, value);
    return error;
  }

  void Bfs::cacheParameter(uint16_t address, uint16_t raw) {
    const auto* parameter = bfs::findParameter(address);
    if (!parameter) return;
    auto& entry = _parameterCache[parameter - bfs::Parameters];
    if (!entry.seen || entry.raw != raw) {
      if (!_cacheDirty) _cacheDirtyAt = nowMs();
      _cacheDirty = true;
    }
    entry = {raw, true};
  }

  void Bfs::clearParameterCache() {
    for (auto& entry : _parameterCache) {
      if (entry.seen) {
        if (!_cacheDirty) _cacheDirtyAt = nowMs();
        _cacheDirty = true;
      }
      entry = {};
    }
  }

  void Bfs::persistParameterCache(bool force) {
    if (!_cacheDirty || (!force && nowMs() - _cacheDirtyAt < 30000)) return;
    _cacheDirty = false;
    // App batches normal config changes. Never force a flash write per read.
    config::Changed::publish(this);
  }

  void Bfs::handleEvent(Event& event) {
    Device::handleEvent(event);
    if (EventPeriodic::is(event)) {
      std::unique_lock guard(_mutex, std::try_to_lock);
      if (guard.owns_lock()) persistParameterCache();
    }
  }

  JsonDocument* Bfs::parameterCache() {
    auto* doc = new JsonDocument();
    (*doc)["cached"] = true;
    auto values = (*doc)["parameters"].to<JsonArray>();
    for (size_t i = 0; i < _parameterCache.size(); ++i) {
      const auto& entry = _parameterCache[i];
      if (!entry.seen) continue;
      auto out = values.add<JsonObject>();
      out["address"] = bfs::Parameters[i].address;
      out["raw"] = entry.raw;
    }
    return doc;
  }

  esp_err_t Bfs::write(uint16_t address, uint16_t value) {
    ESP_CHECK_RETURN(ready());
    // FC06 may have taken effect even when its reply was lost.
    return requestOnce(_address, modbus::Command::WriteRegister, address, value);
  }

  void Bfs::invalidate() {
    for (size_t i = 0; i < _readings.size(); ++i) invalidateChannel(i);
    ++_revision;
  }

  Sensor* Bfs::channelSensor(size_t index) {
    switch (index) {
      case 0: return &_frequency;
      case 2: return &_voltage;
      case 3: return &_current;
      case 5: return &_pressure;
      case 6: return &_temperature;
      default: return nullptr;
    }
  }

  void Bfs::invalidateChannel(size_t index) {
    _readings[index].valid = false;
    if (auto* sensor = channelSensor(index)) sensor->resetState();
  }

  esp_err_t Bfs::readChannel(size_t index) {
    auto& value = _readings[index];
    uint16_t raw = 0;
    const auto error = read(Channels[index].address, raw);
    value.valid = error == ESP_OK;
    if (value.valid) {
      value.raw = raw;
      value.stamp = nowMs();
      value.seen = true;
    }
    auto* sensor = channelSensor(index);
    if (sensor) {
      if (value.valid) sensor->set(raw * Channels[index].scale);
      else sensor->resetState();
    }
    return error;
  }

  bool Bfs::initSensors() {
    std::lock_guard guard(_mutex);
    auto error = _pending ? resolvePending() : ready();
    if (error != ESP_OK) {
      invalidate();
      _lastError = esp_err_to_name(error);
    }
    return error == ESP_OK;
  }

  bool Bfs::pollSensors() {
    unsigned failures = 0, successes = 0;
    for (size_t i = 0; i < _readings.size(); ++i) {
      std::lock_guard guard(_mutex);
      if (failures >= 3) {
        invalidateChannel(i);
        continue;
      }
      const auto error = readChannel(i);
      if (error == ESP_OK) {
        ++successes;
        failures = 0;
      } else {
        ++failures;
        _lastError = esp_err_to_name(error);
      }
    }
    std::lock_guard guard(_mutex);
    if (successes == _readings.size()) _lastError.clear();
    if (!successes) invalidate();
    return successes != 0;
  }

  JsonDocument* Bfs::state() {
    auto doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    root["address"] = _address;
    root["baud"] = _baud;
    root["communicationPending"] = _pending;
    if (!_lastError.empty()) root["lastError"] = _lastError;
    const auto now = nowMs();
    for (size_t i = 0; i < _readings.size(); ++i) {
      const auto& reading = _readings[i];
      auto channel = root[Channels[i].name].to<JsonObject>();
      channel["sampledAtMs"] = reading.seen ? int64_t(reading.stamp) : -1;
      channel["valid"] = reading.valid && reading.seen &&
                         now - reading.stamp <= FreshMs && !_pending;
      if (reading.seen) {
        channel["raw"] = reading.raw;
        channel["value"] = reading.raw * Channels[i].scale;
      }
    }
    root["temperature"]["inferred"] = true;
    return doc;
  }

  JsonDocument* Bfs::getState(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    return state();
  }

  void Bfs::setState(RequestContext& ctx) {
    ctx.errors.check(ESP_ERR_NOT_SUPPORTED,
                     "BFS telemetry is read-only; use the run request for control");
  }

  JsonDocument* Bfs::getDeviceConfig(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    auto doc = new JsonDocument();
    (*doc)["address"] = _address;
    (*doc)["baud"] = _baud;
    if (ctx.request.isInternal()) {
      auto cache = (*doc)["parameterCache"].to<JsonObject>();
      cache["version"] = 1;
      cache["address"] = _address;
      cache["baud"] = _baud;
      auto values = cache["values"].to<JsonArray>();
      for (size_t i = 0; i < _parameterCache.size(); ++i) {
        if (!_parameterCache[i].seen) continue;
        auto row = values.add<JsonArray>();
        row.add(bfs::Parameters[i].address);
        row.add(_parameterCache[i].raw);
      }
    }
    if (_pending) {
      (*doc)["pendingAddress"] = _pendingAddress;
      (*doc)["pendingBaud"] = _pendingBaud;
    }
    // Persistence must stay local. Interactive settings may read the drive,
    // but never change it or repeat a previous write.
    if (!ctx.request.isInternal() && !isDisabled()) {
      auto inspect = [&](uint8_t address, JsonObject details) {
        auto& master = modbus::Master::instance();
        if (!master.isConfigured() || !master.isRunning() ||
            master.config().ser_opts.mode != MB_RTU ||
            master.config().ser_opts.parity != UART_PARITY_DISABLE ||
            master.config().ser_opts.baudrate != _baud) {
          details["address"] = address;
          details["baud"] = _baud;
          details["error"] = ESP_ERR_INVALID_STATE;
        } else
          (void)probe(address, _baud, details);
      };
      inspect(_address, (*doc)["communication"].to<JsonObject>());
      if (_communicationChange)
        (*doc)["lastCommunicationChange"].set(*_communicationChange);
      if (!ctx.data["probeAddress"].isUnbound()) {
        auto target = (*doc)["targetCommunication"].to<JsonObject>();
        if (!ctx.data["probeAddress"].is<unsigned>() ||
            ctx.data["probeAddress"].as<unsigned>() < 1 ||
            ctx.data["probeAddress"].as<unsigned>() > 247)
          target["error"] = ESP_ERR_INVALID_ARG;
        else
          inspect(ctx.data["probeAddress"].as<uint8_t>(), target);
      }
    }
    return doc;
  }

  esp_err_t Bfs::checkProfile() {
    uint16_t protocol;
    ESP_CHECK_RETURN(read(0x030c, protocol));
    return protocol == 1 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
  }

  esp_err_t Bfs::checkStopped() {
    ESP_CHECK_RETURN(readChannel(7));
    ESP_CHECK_RETURN(readChannel(0));
    return (_readings[7].raw & 1) == 0 && _readings[0].raw == 0
               ? ESP_OK : ESP_ERR_INVALID_STATE;
  }

  esp_err_t Bfs::probe(uint8_t address, uint32_t baud, JsonObject details) {
    details["address"] = address;
    details["baud"] = baud;
    auto read = [&]() -> esp_err_t {
      ESP_CHECK_RETURN(modbus::Master::instance().setBaudrate(baud));
      uint16_t foundAddress = 0, foundBaud = 0;
      details["register"] = 0x0307;
      ESP_CHECK_RETURN(requestOnce(address, modbus::Command::ReadHolding,
                                   0x0307, foundAddress));
      details["addressRaw"] = foundAddress;
      details["register"] = 0x0308;
      ESP_CHECK_RETURN(requestOnce(address, modbus::Command::ReadHolding,
                                   0x0308, foundBaud));
      details["baudRaw"] = foundBaud;
      details.remove("register");
      return foundAddress == address && foundBaud == baudSelector(baud)
                 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    };
    const auto error = read();
    details["error"] = error;
    return error;
  }

  esp_err_t Bfs::resolvePending() {
    if (!_pending) return ready();
    if (probe(_pendingAddress, _pendingBaud) == ESP_OK) {
      clearParameterCache();
      _address = _pendingAddress;
      _baud = _pendingBaud;
      _pending = false;
    } else if (probe(_address, _baud) == ESP_OK)
      _pending = false;
    else {
      (void)modbus::Master::instance().setBaudrate(_baud);
      return ESP_ERR_INVALID_STATE;
    }
    invalidate();
    config::Changed::publish(this);
    return ESP_OK;
  }

  esp_err_t Bfs::changeCommunication(uint8_t address, uint32_t baud,
                                    bool& changed, JsonObject details) {
    changed = false;
    details["address"] = address;
    details["baud"] = baud;
    details["writeAttempted"] = false;
    auto checked = [&](esp_err_t error, const char* stage) {
      details["stage"] = stage;
      return error;
    };
    if (_pending) {
      ESP_CHECK_RETURN(checked(resolvePending(),
          "Previous communication change is unresolved; no new write attempted"));
      changed = true;
    }
    if (address == _address && baud == _baud) return ESP_OK;
    ESP_CHECK_RETURN(checked(checkProfile(),
        "Cannot verify Delta-M protocol (F3-12); no write attempted"));
    ESP_CHECK_RETURN(checked(checkStopped(),
        "Drive must report STOP and 0 Hz before changing communication; no write attempted"));
    ESP_CHECK_RETURN(checked(probe(_address, _baud,
        details["before"].to<JsonObject>()),
        "Current F3-07/F3-08 settings could not be verified; no write attempted"));
    auto change = [&](uint16_t reg, uint16_t raw, uint8_t nextAddress,
                      uint32_t nextBaud) -> esp_err_t {
      auto& master = modbus::Master::instance();
      const auto baudError = master.setBaudrate(nextBaud);
      if (baudError != ESP_OK) {
        (void)master.setBaudrate(_baud);
        return checked(baudError,
            "Could not prepare destination baud rate; no write attempted at this step");
      }
      uint16_t occupied = 0;
      const auto occupancyError = requestOnce(
          nextAddress, modbus::Command::ReadHolding, 0x0307, occupied);
      details["destinationProbeError"] = occupancyError;
      if (occupancyError == ESP_OK) details["destinationAddressRaw"] = occupied;
      ESP_CHECK_RETURN(checked(master.setBaudrate(_baud),
          "Could not restore current baud rate; no write attempted at this step"));
      if (occupancyError != ESP_ERR_TIMEOUT)
        return checked(occupancyError == ESP_OK ? ESP_ERR_INVALID_STATE : occupancyError,
            occupancyError == ESP_OK
                ? "Destination already responds; no write attempted at this step"
                : "Destination probe failed without a clean timeout; no write attempted at this step");
      _pendingAddress = nextAddress;
      _pendingBaud = nextBaud;
      details["writeAttempted"] = true;
      details["writeRegister"] = reg;
      details["writeRaw"] = raw;
      const auto writeError = write(reg, raw);
      details["writeError"] = writeError;
      _pending = true;
      changed = true;
      invalidate();
      // Activation and ACK addressing are undocumented. Locate the drive by
      // reading at the desired settings, then the last verified settings.
      if (probe(nextAddress, nextBaud, details["after"].to<JsonObject>()) == ESP_OK) {
        clearParameterCache();
        _address = nextAddress;
        _baud = nextBaud;
        _pending = false;
        return ESP_OK;
      }
      if (probe(_address, _baud, details["previous"].to<JsonObject>()) == ESP_OK)
        _pending = false;
      (void)modbus::Master::instance().setBaudrate(_baud);
      return checked(writeError == ESP_OK ? ESP_ERR_INVALID_RESPONSE : writeError,
          _pending
              ? "Write outcome unknown; neither communication setting verified; do not repeat the write"
              : "New communication settings not verified; previous settings still respond");
    };
    if (address != _address)
      ESP_CHECK_RETURN(change(0x0307, address, address, _baud));
    if (baud != _baud)
      ESP_CHECK_RETURN(change(0x0308, baudSelector(baud), _address, baud));
    return ESP_OK;
  }

  bool Bfs::setDeviceConfig(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    auto data = ctx.data;
    if (!ctx.request.isInternal() && !data["parameterCache"].isUnbound()) {
      ctx.errors.check(ESP_ERR_INVALID_ARG, "Parameter cache is internal observation data");
      return false;
    }
    unsigned address = _address;
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
    if (address < 1 || address > 247 || baudSelector(baud) < 0) {
      ctx.errors.check(ESP_ERR_INVALID_ARG,
                       "Use Modbus address 1..247 and a supported baud rate");
      return false;
    }
    if (ctx.request.isInternal()) {
      const bool pending = !data["pendingAddress"].isUnbound();
      if (pending && (!data["pendingAddress"].is<unsigned>() ||
          data["pendingAddress"].as<unsigned>() < 1 ||
          data["pendingAddress"].as<unsigned>() > 247 ||
          !data["pendingBaud"].is<uint32_t>() ||
          baudSelector(data["pendingBaud"].as<uint32_t>()) < 0)) {
        ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid pending communication settings");
        return false;
      }
      auto error = modbus::Master::instance().setBaudrate(baud);
      if (error != ESP_OK) {
        ctx.errors.check(error, "Cannot restore the configured Modbus baud rate");
        return false;
      }
      _address = address;
      _baud = baud;
      _pending = pending;
      _pendingAddress = data["pendingAddress"].as<uint8_t>();
      _pendingBaud = data["pendingBaud"].as<uint32_t>();
      clearParameterCache();
      const auto cache = data["parameterCache"];
      if (cache["version"].is<unsigned>() && cache["version"].as<unsigned>() == 1 &&
          cache["address"].is<unsigned>() && cache["address"].as<unsigned>() == _address &&
          cache["baud"].is<uint32_t>() && cache["baud"].as<uint32_t>() == _baud &&
          cache["values"].is<JsonArrayConst>() &&
          cache["values"].size() <= bfs::parametersCount) {
        for (auto row : cache["values"].as<JsonArrayConst>()) {
          if (row.is<JsonArrayConst>() && row.size() == 2 &&
              row[0].is<uint16_t>() && row[1].is<uint16_t>())
            cacheParameter(row[0].as<uint16_t>(), row[1].as<uint16_t>());
        }
      }
      _cacheDirty = false;
      invalidate();
      resetSensors(true);
      return true;
    }
    if (ctx.request.isBroadcast()) {
      ctx.errors.check(ESP_ERR_INVALID_ARG, "Target the BFS device explicitly");
      return false;
    }
    bool changed;
    _communicationChange = std::make_unique<JsonDocument>();
    auto details = _communicationChange->to<JsonObject>();
    const auto error = changeCommunication(address, baud, changed, details);
    details["error"] = error;
    if (error == ESP_OK) details["stage"] = "Communication settings verified";
    const char* stage = details["stage"];
    ctx.errors.check(error, "%s (%s, %d)", stage, esp_err_to_name(error), error);
    if (error == ESP_OK) _lastError.clear();
    else _lastError = "Communication change not verified";
    return changed;
  }

  esp_err_t Bfs::validateParameter(uint16_t address, uint16_t raw) {
    const auto* parameter = bfs::findParameter(address);
    if (!parameter || !bfs::validateRaw(*parameter, raw))
      return ESP_ERR_INVALID_ARG;
    uint16_t other;
    auto atMost = [&](uint16_t reg, unsigned divisor = 1,
                      unsigned multiplier = 1, unsigned subtract = 0) {
      auto error = read(reg, other);
      if (error != ESP_OK) return error;
      auto maximum = unsigned(other) * multiplier / divisor;
      return maximum >= subtract && raw <= maximum - subtract
                 ? ESP_OK : ESP_ERR_INVALID_ARG;
    };
    auto atLeast = [&](uint16_t reg) {
      auto error = read(reg, other);
      if (error != ESP_OK) return error;
      return raw >= other ? ESP_OK : ESP_ERR_INVALID_ARG;
    };
    switch (address) {
      case 0x0000: return atMost(0x1008, 10, 9);
      case 0x0001:
        ESP_CHECK_RETURN(atLeast(0x0011));
        return atMost(0x1008, 10, 9);
      case 0x0004:
        ESP_CHECK_RETURN(read(0x0006, other));
        return unsigned(raw) >= unsigned(other) + 10
                   ? ESP_OK : ESP_ERR_INVALID_ARG;
      case 0x0005: return atMost(0x0006);
      case 0x0006:
        ESP_CHECK_RETURN(atLeast(0x0005));
        return atMost(0x0004, 1, 1, 10);
      case 0x0008: return atMost(0x0104);
      case 0x0011: return atMost(0x0001);
      case 0x0017:
        ESP_CHECK_RETURN(atLeast(0x0005));
        return atMost(0x0006);
      case 0x0104:
        ESP_CHECK_RETURN(atLeast(0x0303));
        return atLeast(0x0008);
      case 0x020a: return atLeast(0x020c);
      case 0x020c: return atMost(0x020a);
      case 0x0303: return atMost(0x0104);
      case 0x0400: return atMost(0x0402);
      case 0x0402: return atLeast(0x0400);
      case 0x0405: return atMost(0x0407);
      case 0x0407: return atLeast(0x0405);
      default: return ESP_OK;
    }
  }

  void Bfs::setParameter(Request& request) {
    auto data = request.data();
    if (!data["address"].is<uint16_t>() || !data["raw"].is<uint16_t>() ||
        !data["expectedRaw"].is<uint16_t>()) {
      request.respondError(ESP_ERR_INVALID_ARG, "A parameter, raw value and baseline are required");
      return;
    }
    const auto address = data["address"].as<uint16_t>();
    const auto raw = data["raw"].as<uint16_t>();
    const bool pressure = address == 0x0017;
    auto error = validateParameter(address, raw);
    if (error == ESP_OK) error = checkProfile();
    if (error == ESP_OK && pressure) {
      uint16_t mode = 0, fault = 0;
      error = read(0x000e, mode);
      if (error == ESP_OK && mode != 0) error = ESP_ERR_INVALID_STATE;
      if (error == ESP_OK) error = read(0x2100, fault);
      if (error == ESP_OK && fault != 0) error = ESP_ERR_INVALID_STATE;
    } else if (error == ESP_OK) error = checkStopped();
    uint16_t baseline = 0;
    if (error == ESP_OK) error = read(address, baseline);
    if (error == ESP_OK && baseline != data["expectedRaw"].as<uint16_t>()) {
      request.respondError(ESP_ERR_INVALID_STATE,
                           "Parameter changed since it was read; refresh and edit again");
      return;
    }
    if (error != ESP_OK) {
      request.respondError(error, pressure
          ? "Pressure target requires pressure mode, valid limits and no reported fault"
          : "Parameter is unavailable, protected or outside its limits; stop the drive before editing");
      return;
    }
    JsonDocument doc;
    if (baseline == raw) {
      doc["accepted"] = true;
      doc["verified"] = true;
      doc["raw"] = baseline;
      request.respond(doc.as<JsonVariantConst>(), false);
      return;
    }
    if (address == 0x0306 && raw == 1) {
      const auto stopError = write(0x2000, 1);
      error = checkStopped();
      if (stopError != ESP_OK || error != ESP_OK) {
        request.respondError(stopError != ESP_OK ? stopError : error,
                             "Preloading STOP before changing the command source failed");
        return;
      }
    }
    const auto writeError = write(address, raw);
    uint16_t actual;
    const auto readError = read(address, actual);
    doc["accepted"] = writeError == ESP_OK;
    doc["verified"] = readError == ESP_OK && actual == raw;
    if (writeError != ESP_OK) doc["writeError"] = writeError;
    if (readError == ESP_OK) doc["raw"] = actual;
    else doc["readError"] = readError;
    if (pressure) {
      const auto monitorError = readChannel(4);
      doc["verified"] = doc["verified"].as<bool>() && monitorError == ESP_OK &&
                        _readings[4].raw == raw;
      if (monitorError != ESP_OK) doc["monitorError"] = monitorError;
      else doc["monitorRaw"] = _readings[4].raw;
    }
    if (address == 0x0306 && raw == 1 && readError == ESP_OK && actual == 1) {
      const auto stopError = write(0x2000, 1);
      error = checkStopped();
      doc["sourceVerified"] = error == ESP_OK &&
          (_readings[7].raw & 0x3000) == 0x1000;
      doc["stopAccepted"] = stopError == ESP_OK;
      if (stopError != ESP_OK || !doc["sourceVerified"].as<bool>())
        doc["verified"] = false;
    }
    request.respond(doc.as<JsonVariantConst>(), false);
  }

  void Bfs::run(Request& request) {
    auto data = request.data();
    if (!data["running"].is<bool>() ||
        (data["running"].as<bool>() &&
         (!data["confirm"].is<bool>() || !data["confirm"].as<bool>()))) {
      request.respondError(ESP_ERR_INVALID_ARG, "Confirm starting the pump");
      return;
    }
    const bool running = data["running"].as<bool>();
    if (running) {
      auto error = checkProfile();
      uint16_t source = 0, fault = 0;
      if (error == ESP_OK) error = read(0x0306, source);
      if (error == ESP_OK && source != 1) error = ESP_ERR_INVALID_STATE;
      if (error == ESP_OK) error = read(0x2100, fault);
      if (error == ESP_OK && fault != 0) error = ESP_ERR_INVALID_STATE;
      if (error != ESP_OK) {
        request.respondError(error, "Start requires Delta-M protocol, RS485 command source and no reported current fault");
        return;
      }
    }
    const auto writeError = write(0x2000, running ? 0x0012 : 0x0001);
    const auto statusError = readChannel(7);
    (void)readChannel(0);
    JsonDocument doc;
    doc["accepted"] = writeError == ESP_OK;
    doc["verified"] = statusError == ESP_OK &&
        ((_readings[7].raw & 1) != 0) == running;
    if (writeError != ESP_OK) doc["writeError"] = writeError;
    if (statusError != ESP_OK) doc["readError"] = statusError;
    if (statusError == ESP_OK) doc["running"] = (_readings[7].raw & 1) != 0;
    request.respond(doc.as<JsonVariantConst>(), false);
  }

  void Bfs::startRead(Request& request, bool faults, int address) {
    if (_reading) {
      request.respondError(ESP_ERR_INVALID_STATE, "A BFS dialog read is already in progress");
      return;
    }
    auto* job = new ReadJob{this, std::unique_ptr<Response>(request.makeResponse()),
                            faults, address, _revision};
    _reading = true;
    const auto result = xTaskCreate([](void* argument) {
      std::unique_ptr<ReadJob> job(static_cast<ReadJob*>(argument));
      job->device->readDialog(*job);
      job.reset();
      vTaskDelete(nullptr);
    }, "m/bfs/read", 6144, job, 1, nullptr);
    if (result != pdPASS) {
      _reading = false;
      job->response->setError(ESP_ERR_NO_MEM);
      job->response->publish();
      delete job;
    }
  }

  void Bfs::readDialog(ReadJob& job) {
    auto* doc = new JsonDocument();
    auto values = (*doc)[job.faults ? "values" : "parameters"].to<JsonArray>();
    const auto deadline = nowMs() + DialogBudgetMs;
    unsigned failures = 0;
    bool aborted = false;
    const char* abortReason = nullptr;
    // Static descriptors live in the generated UI catalog. Sending them with
    // every value exhausted internal heap while the UI serialized the reply.
    auto append = [&](uint16_t address) {
      auto out = values.add<JsonObject>();
      out["address"] = address;
      esp_err_t error;
      uint16_t raw = 0;
      {
        std::lock_guard deviceGuard(_deviceMutex);
        std::lock_guard guard(_mutex);
        if (!aborted) {
          if (isDisabled()) abortReason = "disabled";
          else if (_revision != job.revision) abortReason = "changed";
          else if (failures >= 3) abortReason = "failures";
          else if (nowMs() >= deadline) abortReason = "deadline";
          aborted = abortReason != nullptr;
        }
        error = aborted ? ESP_ERR_INVALID_STATE : read(address, raw);
      }
      if (error == ESP_OK) {
        out["raw"] = raw;
        failures = 0;
      } else {
        out["error"] = error;
        if (aborted) out["skipped"] = true;
        ++failures;
      }
      delay(1);
    };
    if (job.faults) {
      for (uint16_t address = 0x1010; address <= 0x1019; ++address)
        append(address);
    } else if (job.address >= 0) {
      auto* parameter = bfs::findParameter(job.address);
      if (parameter) append(parameter->address);
    } else {
      for (const auto& parameter : bfs::Parameters)
        append(parameter.address);
    }
    (*doc)["aborted"] = aborted;
    {
      std::lock_guard guard(_mutex);
      persistParameterCache();
    }
    if (abortReason) (*doc)["abortReason"] = abortReason;
    if (doc->overflowed()) {
      delete doc;
      job.response->setError(ESP_ERR_NO_MEM);
    } else
      job.response->setData(doc);
    {
      std::lock_guard guard(_mutex);
      // The client may enqueue its next single-register read on this reply.
      _reading = false;
    }
    job.response->publish();
  }

  bool Bfs::handleRequest(Request& request) {
    // Cached observations remain available even when the device is disabled.
    if (request.is("parameter-cache")) {
      if (request.isBroadcast()) request.respond(ESP_ERR_INVALID_ARG);
      else {
        std::lock_guard guard(_mutex);
        std::unique_ptr<JsonDocument> doc(parameterCache());
        request.respond(doc->as<JsonVariantConst>(), false);
      }
      return true;
    }
    std::lock_guard deviceGuard(_deviceMutex);
    if (Device::handleRequest(request)) return true;
    if (!request.is("run") && !request.is("faults") &&
        !request.is("parameters") && !request.is("parameter-set")) return false;
    std::lock_guard guard(_mutex);
    if (request.isBroadcast()) {
      request.respondError(ESP_ERR_INVALID_ARG, "Target the BFS device explicitly");
      return true;
    }
    if (request.is("run")) { run(request); persistParameterCache(); }
    else if (request.is("parameter-set")) { setParameter(request); persistParameterCache(true); }
    else if (request.is("faults")) startRead(request, true, -1);
    else {
      const auto address = request.data()["address"];
      if (!address.isUnbound() && (!address.is<uint16_t>() ||
          !bfs::findParameter(address.as<uint16_t>())))
        request.respond(ESP_ERR_INVALID_ARG);
      else startRead(request, false, address.isUnbound() ? -1 : address.as<int>());
    }
    return true;
  }

  Bfs* useBfs(uint8_t address, uint32_t baud, const char* name) {
    return new Bfs(address, baud, name);
  }
}  // namespace esp32m::dev
