#include "esp32m/dev/sm10.hpp"

#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"
#include <esp_timer.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>

namespace esp32m::dev {
  namespace {
    namespace p = sm10::protocol;
    constexpr uint64_t RtcCheckMs = 60000;
    constexpr uint64_t DiscoveryRetryMs = 60000;
    constexpr std::array<uint8_t, 5> Signature{0xce, 0x40, 0x25, 0x04, 0x29};
    uint64_t nowMs() { return esp_timer_get_time() / 1000ULL; }

    std::string rtcText(const p::Rtc& rtc) {
      if (!rtc.valid) return {};
      char text[32];
      std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d",
                    rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second);
      return text;
    }

    bool hostRtc(p::Rtc& rtc) {
      auto now = time(nullptr);
      // Reject the unsynchronized 1970 clock. Manual/system time is allowed;
      // requiring an SNTP instance would exclude other valid clock sources.
      tm local{};
      if (!localtime_r(&now, &local) || local.tm_year < 124 || local.tm_year > 199)
        return false;
      rtc.year = local.tm_year + 1900;
      rtc.month = local.tm_mon + 1;
      rtc.day = local.tm_mday;
      rtc.hour = local.tm_hour;
      rtc.minute = local.tm_min;
      rtc.second = local.tm_sec;
      rtc.weekday = (local.tm_wday + 6) % 7;
      rtc.valid = p::validRtc(rtc);
      return rtc.valid;
    }

    time_t calendarTime(const p::Rtc& rtc) {
      tm local{};
      local.tm_year = rtc.year - 1900;
      local.tm_mon = rtc.month - 1;
      local.tm_mday = rtc.day;
      local.tm_hour = rtc.hour;
      local.tm_min = rtc.minute;
      local.tm_sec = rtc.second;
      local.tm_isdst = -1;
      return mktime(&local);
    }

    struct EbContext {
      p::Command command;
      p::Tuple tuple;
      p::EbReply* reply;
      static bool complete(const uint8_t* bytes, size_t size, void* context) {
        auto& c = *static_cast<EbContext*>(context);
        return p::findEbReply(bytes, size, c.command, c.tuple, *c.reply) ==
               p::ParseResult::Complete;
      }
    };

    size_t ebReplyBytes(p::Command command) {
      switch (command) {
        case p::Command::HistoryHourly: return 210;
        case p::Command::HistoryDaily: return 132;
        case p::Command::HistoryMonthly: return 104;
        case p::Command::HistoryYearly: return 56;
        case p::Command::Identity: return 22;
        default: return 26;
      }
    }

    esp_err_t exchange(const p::Frame& frame, uint32_t baud, size_t replyBytes,
                        bool (*complete)(const uint8_t*, size_t, void*),
                        void* context) {
      std::array<uint8_t, 256> response{};
      size_t received = 0;
      // Include serialization time at 1200 baud, including long history banks.
      auto timeout = 1500U + uint32_t((replyBytes * 10000 + baud - 1) / baud);
      return modbus::Master::instance().exchangeRaw(
          frame.bytes.data(), frame.size, response.data(), response.size(),
          &received, baud, timeout, complete, context);
    }

    bool sameFine(const p::Fine& a, const p::Fine& b) {
      return a.total == b.total && a.fraction == b.fraction;
    }
  }  // namespace

  Sm10::Sm10(uint8_t address, uint32_t baud, const char* name)
      : _name(name ? name : "SM10"), _baud(baud),
        _flow(this, "volume_flow_rate", "flow"),
        _total(this, "water", "total"),
        _temperature(this, "temperature", "temperature"),
        _voltage(this, "voltage", "voltage") {
    p::baudSelector(baud, _identity.tuple[0]);
    _identity.tuple[3] = address;
    _flow.unit = "m³/h";
    _flow.precision = 5;
    _flow.stateClass = StateClass::Measurement;
    _total.unit = "m³";
    _total.precision = 8;
    _total.stateClass = StateClass::Total;
    _temperature.unit = "°C";
    _temperature.precision = 1;
    _temperature.stateClass = StateClass::Measurement;
    _temperature.setTitle("Internal temperature");
    _voltage.unit = "V";
    _voltage.precision = 3;
    _voltage.stateClass = StateClass::Measurement;
    _voltage.setTitle("Monitored supply voltage");
    setSensorsPollInterval(5000);
    Device::init(Flags::HasSensors | Flags::CanDisable);
  }

  esp_err_t Sm10::readEb(p::Command command, p::EbReply& reply) {
    p::Frame frame;
    if (!p::buildRead(command, _identity.tuple, frame))
      return ESP_ERR_INVALID_ARG;
    EbContext context{command, _identity.tuple, &reply};
    esp_err_t error = ESP_FAIL;
    for (int attempt = 0; attempt < 2; ++attempt) {
      error = exchange(frame, _baud, ebReplyBytes(command), EbContext::complete, &context);
      if (error == ESP_OK) break;
    }
    return error;
  }

  esp_err_t Sm10::writeEb(const p::Frame& frame, p::Command command,
                          const p::Tuple& expected) {
    p::EbReply reply;
    EbContext context{command, expected, &reply};
    // Never retry a setting after an uncertain transmission or missing ACK.
    return exchange(frame, _baud, 8, EbContext::complete, &context);
  }

  esp_err_t Sm10::identify() {
    uint8_t selector;
    if (!p::isValidId(_identity.tuple[3]) || !p::baudSelector(_baud, selector))
      return ESP_ERR_INVALID_ARG;
    p::EbReply reply;
    ESP_CHECK_RETURN(readEb(p::Command::Identity, reply));
    p::Identity found;
    if (!p::decodeIdentity(reply, found) || found.signature != Signature ||
        p::baudRate(found.tuple[0]) != _baud)
      return ESP_ERR_INVALID_RESPONSE;
    _identity = found;
    _identified = true;
    return ESP_OK;
  }

  esp_err_t Sm10::discover() {
    // Only initialization/reconnection may discover. Writes and uncertain
    // write recovery must keep addressing the exact meter selected beforehand.
    _nextDiscoveryAt = nowMs() + DiscoveryRetryMs;
    p::Frame frame;
    if (!p::buildRead(p::Command::Identity, p::DiscoveryTuple, frame))
      return ESP_ERR_INVALID_ARG;
    std::array<uint8_t, 256> bytes{};
    size_t received = 0;
    const auto timeout = 1500U + uint32_t((22U * 10000 + _baud - 1) / _baud);
    // Collect the entire bounded window: accepting the first valid frame
    // would hide a second responding meter on the shared bus.
    ESP_CHECK_RETURN(modbus::Master::instance().exchangeRaw(
        frame.bytes.data(), frame.size, bytes.data(), bytes.size(), &received,
        _baud, timeout, nullptr, nullptr));
    if (received == bytes.size())
      return ESP_ERR_INVALID_RESPONSE;  // Overflow may conceal another reply.
    size_t offset = 0;
    while (offset < received && bytes[offset] == 0xfe) ++offset;
    // A single identity frame, optionally preceded by wakeup FE bytes. Reject
    // echoes, noise, truncated replies and multiple frames, including copies.
    if (received - offset != 22)
      return ESP_ERR_INVALID_RESPONSE;
    p::EbReply reply;
    p::Identity candidate;
    if (p::findEbReply(bytes.data() + offset, received - offset,
          p::Command::Identity, p::DiscoveryTuple, reply, true) !=
            p::ParseResult::Complete ||
        !p::decodeIdentity(reply, candidate) || candidate.signature != Signature ||
        candidate.tuple[3] != _identity.tuple[3] ||
        p::baudRate(candidate.tuple[0]) != _baud)
      return ESP_ERR_INVALID_RESPONSE;

    const auto previous = _identity;
    _identity = candidate;
    _identified = false;
    auto error = identify();  // Exact addressed confirmation, no wildcard.
    if (error == ESP_OK && _identity.upper != candidate.upper)
      error = ESP_ERR_INVALID_RESPONSE;
    p::Fine fine;
    if (error == ESP_OK) error = readFine(fine);
    if (error != ESP_OK) {
      _identity = previous;
      _identified = false;
    }
    return error;
  }

  esp_err_t Sm10::resolvePending() {
    if (!_pending) {
      const auto previous = _identity;
      auto error = identify();
      if (error == ESP_ERR_TIMEOUT && nowMs() >= _nextDiscoveryAt)
        error = discover();
      if (error == ESP_OK && (previous.tuple != _identity.tuple ||
                              previous.upper != _identity.upper))
        config::Changed::publish(this);
      return error;
    }
    const auto old = _identity;
    const auto oldBaud = _baud;
    _identity.tuple[3] = _pendingAddress;
    // Other selectors also mean 1200; retain the exact tuple for ID-only
    // changes, just as the original setting frame did.
    if (_pendingBaud != oldBaud)
      p::baudSelector(_pendingBaud, _identity.tuple[0]);
    _baud = _pendingBaud;
    auto error = identify();
    if (error != ESP_OK) {
      _identity = old;
      _baud = oldBaud;
      error = identify();
    }
    if (error == ESP_OK) {
      _pending = false;
      config::Changed::publish(this);
    }
    return error;
  }

  bool Sm10::initSensors() {
    std::lock_guard guard(_mutex);
    const auto error = resolvePending();
    if (error != ESP_OK) invalidate(error, "Identify meter");
    return error == ESP_OK;
  }

  esp_err_t Sm10::readFine(p::Fine& result) {
    p::EbReply reply;
    ESP_CHECK_RETURN(readEb(p::Command::Fine, reply));
    return p::decodeFine(reply, result) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
  }

  esp_err_t Sm10::readFlow(double& flow) {
    p::Frame frame;
    if (!p::buildFlowRead(_identity.tuple[3], frame)) return ESP_ERR_INVALID_ARG;
    struct Context {
      uint8_t id;
      double litresPerHour = 0;
      static bool complete(const uint8_t* bytes, size_t size, void* context) {
        auto& c = *static_cast<Context*>(context);
        return p::findFlowReply(bytes, size, c.id, c.litresPerHour) ==
               p::ParseResult::Complete;
      }
    } context{_identity.tuple[3]};
    esp_err_t error = ESP_FAIL;
    for (int attempt = 0; attempt < 2; ++attempt) {
      error = exchange(frame, _baud, 9, Context::complete, &context);
      if (error == ESP_OK) {
        flow = context.litresPerHour / 1000.0;
        break;
      }
    }
    return error;
  }

  esp_err_t Sm10::readRtc() {
    p::Frame frame;
    struct Context {
      p::Address address;
      p::Rtc rtc;
      static bool complete(const uint8_t* bytes, size_t size, void* context) {
        auto& c = *static_cast<Context*>(context);
        return p::findRtcReply(bytes, size, c.address, 1, c.rtc) ==
               p::ParseResult::Complete;
      }
    } context{p::address(_identity), {}};
    p::buildRtcRead(context.address, 1, frame);
    auto error = exchange(frame, _baud, 23, Context::complete, &context);
    if (error == ESP_OK) {
      _rtc = context.rtc;
      _rtc.valid = _rtc.valid && _rtc.weekday ==
          p::calendarWeekday(_rtc.year, _rtc.month, _rtc.day);
      _rtcStamp = nowMs();
      if (!_rtc.valid) _rtcSync = "invalid";
    } else
      _rtcSync = "read-failed";
    return error;
  }

  esp_err_t Sm10::checkRtc() {
    _nextRtcCheck = nowMs() + RtcCheckMs;
    auto error = readRtc();
    if (error != ESP_OK) {
      _rtcSync = "read-failed";
      return error;
    }
    if (_rtc.valid) {
      if (strcmp(_rtcSync, "synced")) _rtcSync = "valid";
      return ESP_OK;
    }
    p::Rtc desired;
    if (!hostRtc(desired)) {
      _rtcSync = "waiting-for-time";
      return ESP_OK;
    }
    p::Frame frame;
    if (!p::buildSetRtc(_identity.tuple, desired, frame))
      return ESP_ERR_INVALID_ARG;
    (void)writeEb(frame, p::Command::SetRtc, _identity.tuple);
    // The meter's ACK is not a success proof; it has a broken setter verifier.
    error = readRtc();
    if (error == ESP_OK && (!_rtc.valid ||
        std::abs(difftime(calendarTime(_rtc), time(nullptr))) > 5))
      error = ESP_ERR_INVALID_RESPONSE;
    _rtcSync = error == ESP_OK ? "synced" : "sync-failed";
    return error;
  }

  void Sm10::invalidate(esp_err_t error, const char* operation) {
    _hasReading = false;
    _identified = false;
    _lastError = std::string(operation) + ": " + esp_err_to_name(error);
    _flow.resetState();
    _total.resetState();
    _temperature.resetState();
    _voltage.resetState();
  }

  void Sm10::publishReadings() {
    _stamp = nowMs();
    _hasReading = true;
    _flow.set(_flowValue);
    _total.set(_fine.volumeM3);  // Keep double precision end-to-end.
    _temperature.set(_fine.temperatureC);
    _voltage.set(_fine.voltageMv / 1000.0);
  }

  bool Sm10::pollSensors() {
    std::lock_guard guard(_mutex);
    auto error = _identified && !_pending ? ESP_OK : resolvePending();
    p::Fine fine;
    double flow = 0;
    if (error == ESP_OK) error = readFine(fine);
    if (error == ESP_OK) error = readFlow(flow);
    if (error != ESP_OK) {
      invalidate(error, "Read meter");
      return false;
    }
    _fine = fine;
    _flowValue = flow;
    _lastError.clear();
    publishReadings();
    if (nowMs() >= _nextRtcCheck) {
      p::EbReply reply;
      error = readEb(p::Command::Status, reply);
      if (error == ESP_OK && !p::decodeStatus(reply, _status))
        error = ESP_ERR_INVALID_RESPONSE;
      _hasStatus = error == ESP_OK;
      if (error != ESP_OK)
        _lastError = std::string("Read status: ") + esp_err_to_name(error);
      error = checkRtc();
      if (error != ESP_OK)
        _lastError = std::string("Check RTC: ") + esp_err_to_name(error);
    }
    return true;
  }

  JsonDocument* Sm10::state() {
    auto doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    root["sampledAtMs"] = _hasReading ? int64_t(_stamp) : -1;
    root["address"] = _identity.tuple[3];
    root["baud"] = _baud;
    if (_hasReading) {
      root["flow"] = _flowValue;
      root["total"] = _fine.volumeM3;
      root["temperature"] = _fine.temperatureC;
      root["voltage"] = _fine.voltageMv / 1000.0;
    }
    if (_hasStatus) {
      root["status"] = _status.status;
      root["bootCount"] = _status.bootCount;
    }
    root["rtc"] = nullptr;
    if (_rtc.valid) root["rtc"] = rtcText(_rtc);
    root["rtcValid"] = _rtc.valid;
    root["rtcSampledAtMs"] = _rtcStamp ? int64_t(_rtcStamp) : -1;
    root["rtcSync"] = _rtcSync;
    if (!_lastError.empty()) root["lastError"] = _lastError;
    return doc;
  }

  JsonDocument* Sm10::getState(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    return state();
  }

  JsonDocument* Sm10::getDeviceConfig(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    auto doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    root["address"] = _identity.tuple[3];
    root["baud"] = _baud;
    // Preserve opaque address bytes without exposing editable UI fields.
    auto tuple = root["tuple"].to<JsonArray>();
    for (auto b : _identity.tuple) tuple.add(b);
    auto upper = root["upper"].to<JsonArray>();
    for (auto b : _identity.upper) upper.add(b);
    if (_pending) {
      root["pendingAddress"] = _pendingAddress;
      root["pendingBaud"] = _pendingBaud;
    }
    return doc;
  }

  esp_err_t Sm10::checkIdle(p::Fine& result) {
    if (_pending) return ESP_ERR_INVALID_STATE;
    ESP_CHECK_RETURN(identify());
    p::Fine before;
    double flow = 0;
    ESP_CHECK_RETURN(readFine(before));
    ESP_CHECK_RETURN(readFlow(flow));
    ESP_CHECK_RETURN(readFine(result));
    if (flow != 0 || !sameFine(before, result)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
  }

  esp_err_t Sm10::changeMeter(uint8_t address, uint32_t baud,
                              const uint32_t* totalCoarse, bool& configChanged) {
    configChanged = false;
    p::Fine before;
    ESP_CHECK_RETURN(checkIdle(before));
    const auto original = _identity;
    const auto originalBaud = _baud;
    auto desired = original;
    desired.tuple[3] = address;
    if (baud != originalBaud && !p::baudSelector(baud, desired.tuple[0]))
      return ESP_ERR_INVALID_ARG;
    auto coarse = totalCoarse ? *totalCoarse : before.coarse;
    p::Frame frame;
    if (!p::buildSetConfigTotal(original.tuple, desired, coarse, frame))
      return ESP_ERR_INVALID_ARG;
    configChanged = original.tuple != desired.tuple;
    if (configChanged) {
      _pending = true;
      _pendingAddress = address;
      _pendingBaud = baud;
    }
    // ACK may use the new tuple/baud. Always verify at the desired settings.
    (void)writeEb(frame, p::Command::SetConfigTotal, desired.tuple);
    _identity = desired;
    _baud = baud;
    _identified = false;
    auto error = identify();
    if (error != ESP_OK) {
      _identity = original;
      _baud = originalBaud;
      if (identify() == ESP_OK) _pending = false;
      invalidate(error, "Write outcome unverified; refresh before retrying");
      return error;
    }
    _pending = false;
    error = readFine(_fine);
    if (error == ESP_OK && (_fine.total != coarse * 10 || _fine.fraction != 0))
      error = ESP_ERR_INVALID_RESPONSE;
    if (error == ESP_OK) error = readFlow(_flowValue);
    if (error != ESP_OK) {
      invalidate(error, "Meter settings applied but total readback failed");
      return error;
    }
    _lastError.clear();
    publishReadings();
    return ESP_OK;
  }

  bool Sm10::setDeviceConfig(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    auto cfg = ctx.data;
    unsigned address = _identity.tuple[3];
    uint32_t baud = _baud;
    uint8_t selector;
    if (!cfg["address"].isUnbound()) {
      if (!cfg["address"].is<unsigned>()) {
        ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid slave ID");
        return false;
      }
      address = cfg["address"].as<unsigned>();
    }
    if (!cfg["baud"].isUnbound()) {
      if (!cfg["baud"].is<uint32_t>()) {
        ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid baud rate");
        return false;
      }
      baud = cfg["baud"].as<uint32_t>();
    }
    if (!p::isValidId(address) || !p::baudSelector(baud, selector)) {
      ctx.errors.check(ESP_ERR_INVALID_ARG, "Use ID 1..247 except 235 and a supported baud");
      return false;
    }
    if (ctx.request.isInternal()) {
      auto restored = _identity;
      restored.tuple[3] = address;
      restored.tuple[0] = selector;
      auto restoreBytes = [](JsonVariantConst value, auto& bytes) {
        if (value.isUnbound()) return true;
        auto array = value.as<JsonArrayConst>();
        if (array.size() != bytes.size()) return false;
        for (size_t i = 0; i < bytes.size(); ++i) {
          if (!array[i].template is<uint8_t>()) return false;
          bytes[i] = array[i].template as<uint8_t>();
        }
        return true;
      };
      if (!restoreBytes(cfg["tuple"], restored.tuple) ||
          !restoreBytes(cfg["upper"], restored.upper) ||
          restored.tuple[3] != address || p::baudRate(restored.tuple[0]) != baud) {
        ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid saved identity");
        return false;
      }
      bool pending = !cfg["pendingAddress"].isUnbound();
      if (pending && (!cfg["pendingAddress"].is<unsigned>() ||
          !p::isValidId(cfg["pendingAddress"].as<unsigned>()) ||
          !cfg["pendingBaud"].is<uint32_t>() ||
          !p::baudSelector(cfg["pendingBaud"].as<uint32_t>(), selector))) {
        ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid pending identity");
        return false;
      }
      _identity = restored;
      _baud = baud;
      _pending = pending;
      _pendingAddress = cfg["pendingAddress"].as<uint8_t>();
      _pendingBaud = cfg["pendingBaud"].as<uint32_t>();
      _identified = false;
      _nextRtcCheck = 0;
      return true;  // Local restoration never writes to the meter.
    }
    if (address == _identity.tuple[3] && baud == _baud && !_pending) return false;
    if (!cfg["confirm"].is<bool>() || !cfg["confirm"].as<bool>()) {
      ctx.errors.check(ESP_ERR_INVALID_ARG, "Confirm loss of the total fraction below 0.1 L");
      return false;
    }
    bool changed = false;
    auto error = changeMeter(address, baud, nullptr, changed);
    ctx.errors.check(error, "Stop flow before changing settings; a failed write must be checked before retrying");
    return changed;
  }

  bool Sm10::handleRequest(Request& request) {
    std::lock_guard deviceGuard(_deviceMutex);
    if (Device::handleRequest(request)) return true;
    if (!request.is("history") && !request.is("set-total")) return false;
    std::lock_guard guard(_mutex);
    if (request.is("set-total")) {
      auto data = request.data();
      uint32_t coarse = 0;
      const auto total = data["total"];
      bool validTotal = false;
      if (total.is<JsonString>()) {
        const auto text = total.as<JsonString>();
        validTotal = p::totalToCoarse(text.c_str(), text.size(), coarse);
      } else if (total.is<double>()) {
        // Preserve strict legacy numeric handling; widening the tolerance can
        // silently change a requested total after JSON float narrowing.
        validTotal = p::totalToCoarse(total.as<double>(), coarse);
      }
      if (request.isBroadcast() || !data["confirm"].is<bool>() || !data["confirm"].as<bool>() ||
          !validTotal) {
        request.respondError(ESP_ERR_INVALID_ARG,
            "Confirm a total of 0..9999.9999 m3 in 0.0001 m3 steps; send decimal text for exact precision");
        return true;
      }
      bool changed = false;
      auto error = changeMeter(_identity.tuple[3], _baud, &coarse, changed);
      if (error != ESP_OK)
        request.respondError(error, "Total change not verified. Stop flow and refresh before retrying.");
      else {
        std::unique_ptr<JsonDocument> doc(state());
        request.respond(doc->as<JsonVariantConst>(), false);
      }
      return true;
    }
    const char* bank = request.data()["bank"].as<const char*>();
    p::Command command;
    if (bank && !strcmp(bank, "hourly")) command = p::Command::HistoryHourly;
    else if (bank && !strcmp(bank, "daily")) command = p::Command::HistoryDaily;
    else if (bank && !strcmp(bank, "monthly")) command = p::Command::HistoryMonthly;
    else if (bank && !strcmp(bank, "yearly")) command = p::Command::HistoryYearly;
    else {
      request.respond(ESP_ERR_INVALID_ARG);
      return true;
    }
    auto error = _identified && !_pending ? ESP_OK : resolvePending();
    p::EbReply reply;
    p::History history;
    if (error == ESP_OK) error = readEb(command, reply);
    if (error == ESP_OK && !p::decodeHistory(reply, history))
      error = ESP_ERR_INVALID_RESPONSE;
    // Return a fresh timestamp/current total alongside the on-demand bank.
    p::Fine current;
    if (error == ESP_OK) error = readFine(current);
    if (error == ESP_OK) error = readRtc();
    if (error != ESP_OK) request.respond(error);
    else {
      JsonDocument doc;
      doc["bank"] = bank;
      auto values = doc["values"].to<JsonArray>();
      for (size_t i = 0; i < history.count; ++i)
        values.add(history.coarse[i] / 10000.0);
      doc["current"] = current.volumeM3;
      doc["rtc"] = nullptr;
      if (_rtc.valid) doc["rtc"] = rtcText(_rtc);
      request.respond(doc.as<JsonVariantConst>(), false);
    }
    return true;
  }

  Sm10* useSm10(uint8_t address, uint32_t baud, const char* name) {
    return new Sm10(address, baud, name);
  }
}  // namespace esp32m::dev
