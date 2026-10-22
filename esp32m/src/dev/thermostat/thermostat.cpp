#include "esp32m/dev/thermostat.hpp"
#include "esp32m/dev/thermostat/controller.hpp"
#include "esp32m/integrations/ha/ha.hpp"
#include "esp32m/net/mqtt.hpp"
#include "esp32m/config/config.hpp"
#include <esp_timer.h>

#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>

namespace esp32m::dev {
namespace th = thermostat;
namespace {
uint64_t thermostatNow() { return esp_timer_get_time() / 1000ULL; }
}

class ThermostatClimate : public Component {
 public:
  explicit ThermostatClimate(Device* device) : Component("climate") {
    // Publish fresh local state; a retained old action must not imply ownership.
    _flags = Flags(Flags::HasState | Flags::AcceptsCommands);
    Component::init(device);
    setTitle("Room thermostat");
  }
  const char* component() const override { return "climate"; }
  const char* type() const override { return ""; }
};

class ThermostatReady : public BinarySensor {
 public:
  explicit ThermostatReady(Device* device) : BinarySensor(device, "connectivity", "ready") {
    _flags = Flags(unsigned(_flags) & ~unsigned(Flags::RetainState));
  }
};

namespace {
const char* resultName(th::Result result) {
  switch (result) {
    case th::Result::Accepted: return "accepted";
    case th::Result::Busy: return "busy";
    case th::Result::Unsupported: return "unsupported";
    case th::Result::Unavailable: return "unavailable";
    case th::Result::Invalid: return "invalid";
    case th::Result::Rejected: return "rejected";
  }
  return "unknown";
}

void configToJson(JsonObject root, const th::Config& config) {
  root["enabled"] = config.enabled;
  root["heat"] = config.heat;
  root["shadow"] = config.shadow;
  root["allowFullShutdown"] = config.allowFullShutdown;
  root["sensorUid"] = config.sensorUid;
  root["strategy"] = th::strategyName(config.strategy);
  root["targetC"] = config.targetC;
  root["heatHysteresisC"] = config.heatHysteresisC;
  root["stopHysteresisC"] = config.stopHysteresisC;
  root["proportionalBandC"] = config.proportionalBandC;
  root["highTemperatureC"] = config.highTemperatureC;
  root["minimumFlowC"] = config.minimumFlowC;
  root["maximumFlowC"] = config.maximumFlowC;
  root["minimumRunMs"] = config.minimumRunMs;
  root["minimumOffMs"] = config.minimumOffMs;
  root["lowPowerHoldMs"] = config.lowPowerHoldMs;
  root["sensorFreshnessMs"] = config.sensorFreshnessMs;
  root["feedbackFreshnessMs"] = config.feedbackFreshnessMs;
  root["commandTimeoutMs"] = config.commandTimeoutMs;
}

// Parse into a copy so a malformed combined update changes no settings.
bool configFromJson(JsonObjectConst data, th::Config& config) {
  for (JsonPairConst pair : data) {
    const char* key = pair.key().c_str();
    auto value = pair.value();
    if (!strcmp(key, "enabled") || !strcmp(key, "heat") ||
        !strcmp(key, "shadow") || !strcmp(key, "allowFullShutdown")) {
      if (!value.is<bool>()) return false;
      const bool v = value.as<bool>();
      if (!strcmp(key, "enabled")) config.enabled = v;
      else if (!strcmp(key, "heat")) config.heat = v;
      else if (!strcmp(key, "shadow")) config.shadow = v;
      else config.allowFullShutdown = v;
    } else if (!strcmp(key, "sensorUid")) {
      if (!value.is<const char*>() || strlen(value.as<const char*>()) > 192) return false;
      config.sensorUid = value.as<const char*>();
    } else if (!strcmp(key, "strategy")) {
      const auto v = value.as<const char*>();
      if (!v) return false;
      if (!strcmp(v, "automatic")) config.strategy = th::Strategy::Automatic;
      else if (!strcmp(v, "native")) config.strategy = th::Strategy::Native;
      else if (!strcmp(v, "power")) config.strategy = th::Strategy::Power;
      else if (!strcmp(v, "flow")) config.strategy = th::Strategy::Flow;
      else return false;
    } else if (!strcmp(key, "targetC") || !strcmp(key, "heatHysteresisC") ||
               !strcmp(key, "stopHysteresisC") || !strcmp(key, "proportionalBandC") ||
               !strcmp(key, "highTemperatureC") || !strcmp(key, "minimumFlowC") ||
               !strcmp(key, "maximumFlowC")) {
      if (!value.is<float>()) return false;
      const float v = value.as<float>();
      if (!std::isfinite(v)) return false;
      if (!strcmp(key, "targetC")) config.targetC = v;
      else if (!strcmp(key, "heatHysteresisC")) config.heatHysteresisC = v;
      else if (!strcmp(key, "stopHysteresisC")) config.stopHysteresisC = v;
      else if (!strcmp(key, "proportionalBandC")) config.proportionalBandC = v;
      else if (!strcmp(key, "highTemperatureC")) config.highTemperatureC = v;
      else if (!strcmp(key, "minimumFlowC")) config.minimumFlowC = v;
      else config.maximumFlowC = v;
    } else if (!strcmp(key, "minimumRunMs") || !strcmp(key, "minimumOffMs") ||
               !strcmp(key, "lowPowerHoldMs") || !strcmp(key, "sensorFreshnessMs") ||
               !strcmp(key, "feedbackFreshnessMs") || !strcmp(key, "commandTimeoutMs")) {
      if (!value.is<uint32_t>()) return false;
      const auto v = value.as<uint32_t>();
      if (!strcmp(key, "minimumRunMs")) config.minimumRunMs = v;
      else if (!strcmp(key, "minimumOffMs")) config.minimumOffMs = v;
      else if (!strcmp(key, "lowPowerHoldMs")) config.lowPowerHoldMs = v;
      else if (!strcmp(key, "sensorFreshnessMs")) config.sensorFreshnessMs = v;
      else if (!strcmp(key, "feedbackFreshnessMs")) config.feedbackFreshnessMs = v;
      else config.commandTimeoutMs = v;
    } else return false;
  }
  return th::validConfig(config);
}

th::Temperature temperatureSample(const Sensor& sensor) {
  const auto sample = sensor.sample();
  th::Temperature result;
  result.sampledAtMs = sample.sampledAtMs;
  if (!sample.valid || sensor.isDisabled() || !std::isfinite(sample.value)) return result;
  const char* unit = sensor.unit;
  double value = sample.value;
  if (!unit || !*unit || !strcmp(unit, "°C") || !strcmp(unit, "C") ||
      !strcmp(unit, "celsius")) {}
  else if (!strcmp(unit, "°F") || !strcmp(unit, "F")) value = (value - 32) * 5 / 9;
  else if (!strcmp(unit, "K")) value -= 273.15;
  else return result;
  result.celsius = static_cast<float>(value);
  result.valid = std::isfinite(result.celsius) && result.celsius >= -80 && result.celsius <= 150;
  return result;
}
}  // namespace

class Thermostat::Impl {
 public:
  Impl(Thermostat* owner, th::Actuator* actuator,
       std::unique_ptr<th::Actuator> ownedActuator = {})
      : _owner(owner), _ownedActuator(std::move(ownedActuator)),
        _actuator(_ownedActuator ? _ownedActuator.get() : actuator) {
    _attached = _actuator && _actuator->attachThermostat(owner);
    if (_attached) {
      // A rejected duplicate must never overwrite the original component UIDs,
      // including when both callers used the same default device name.
      _climate.emplace(owner);
      _ready.emplace(owner);
      _diagnostic.emplace(owner, "", "status");
      _ready->setTitle("Thermostat ready");
      _diagnostic->setTitle("Thermostat status");
    }
    if (_actuator && !_actuator->capabilities().deviceGate) _config.heat = false;
  }
  ~Impl() {
    // Release only a lease acquired by this instance, and never adopt a newer
    // manual revision merely to make a release pass its ownership guard.
    if (_attached && _ownsOutput && _actuator &&
        _actuator->snapshot().revision == _ownedRevision) {
      th::Request release;
      release.command = th::Command::Release;
      release.expectedRevision = _ownedRevision;
      release.validUntilMs = thermostatNow() + 5000;
      _actuator->apply(release);
    }
    if (_attached) _actuator->detachThermostat(_owner);
  }
  bool rejected() const { return _actuator && !_attached; }

  bool configure(JsonObjectConst data, bool& changed, bool restoring) {
    std::lock_guard guard(_mutex);
    auto proposed = _config;
    if (!configFromJson(data, proposed)) return false;
    if (restoring && _actuator && !_actuator->capabilities().deviceGate) proposed.heat = false;
    JsonDocument previous, next;
    configToJson(previous.to<JsonObject>(), _config);
    configToJson(next.to<JsonObject>(), proposed);
    changed = previous != next;
    const bool rearm = (!_config.enabled && proposed.enabled) ||
        _config.sensorUid != proposed.sensorUid || _config.strategy != proposed.strategy ||
        _config.shadow != proposed.shadow;
    _config = proposed;
    if (rearm) _controller.reset();
    return true;
  }

  bool command(JsonObjectConst data, bool& changed) {
    if (data.size() != 1) return false;
    if (data["resume"].is<bool>() && data["resume"].as<bool>()) {
      std::lock_guard guard(_mutex);
      _controller.reset();
      changed = false;
      return true;
    }
    JsonDocument config;
    if (data["mode"].is<const char*>()) {
      const auto mode = data["mode"].as<const char*>();
      if (strcmp(mode, "off") && strcmp(mode, "heat")) return false;
      config["heat"] = !strcmp(mode, "heat");
    } else if (data["temperature"].is<float>()) config["targetC"] = data["temperature"];
    else return false;
    return configure(config.as<JsonObjectConst>(), changed, false);
  }

  JsonDocument* configuration(bool persisted) {
    std::lock_guard guard(_mutex);
    auto* doc = new JsonDocument();
    configToJson(doc->to<JsonObject>(), _config);
    // Diesel restart is a deliberate user action after reboot. An OpenTherm
    // backend may restore heat intent; its own CH gate remains authoritative.
    if (persisted && _actuator && !_actuator->capabilities().deviceGate) (*doc)["heat"] = false;
    return doc;
  }

  void sources(JsonArray target, uint64_t now) {
    AllComponents components;
    for (auto* component : components) {
      if (component->device() == _owner || !component->isSensor() ||
          strcmp(component->type(), "temperature")) continue;
      const auto* sensor = static_cast<const Sensor*>(component);
      const auto sample = temperatureSample(*sensor);
      auto item = target.add<JsonObject>();
      item["uid"] = sensor->uid();
      item["name"] = sensor->title() ? sensor->title() : sensor->id();
      item["device"] = sensor->device()->name();
      item["enabled"] = !sensor->isDisabled();
      item["valid"] = sample.valid && now >= sample.sampledAtMs &&
          now - sample.sampledAtMs <= _config.sensorFreshnessMs;
      if (sample.valid) item["valueC"] = sample.celsius; else item["valueC"] = nullptr;
      if (sample.sampledAtMs && now >= sample.sampledAtMs) item["sampledAtMs"] = sample.sampledAtMs;
      else item["sampledAtMs"] = nullptr;
    }
  }

  void tick() {
    JsonDocument climate;
    bool ready;
    const char* reason;
    {
      std::lock_guard guard(_mutex);
      const auto now = thermostatNow();
      _input = {};
      _input.nowMs = now;
      if (_actuator) {
        _input.capabilities = _actuator->capabilities();
        _input.feedback = _actuator->snapshot();
      }
      _input.sourceIsNative = _config.sensorUid.empty();
      if (_input.sourceIsNative) _input.temperature = _input.feedback.nativeTemperature;
      else {
        AllComponents components;
        for (auto* component : components) {
          if (component->device() == _owner || !component->isSensor() ||
              strcmp(component->type(), "temperature") || component->uid() != _config.sensorUid) continue;
          _input.temperature = temperatureSample(*static_cast<Sensor*>(component));
          break;
        }
      }
      if (_attached) _decision = _controller.evaluate(_config, _input);
      else {
        _decision = {};
        _decision.inhibited = true;
        _decision.reason = _actuator ? "actuator-already-attached" : "no-actuator";
      }
      if (_attached && !_config.shadow && _decision.request.command != th::Command::None) {
        _lastCommand = th::commandName(_decision.request.command);
        const auto result = _actuator->apply(_decision.request);
        _lastResult = resultName(result);
        if (result == th::Result::Accepted) {
          _ownsOutput = _decision.request.command != th::Command::Release;
          _ownedRevision = _decision.request.expectedRevision;
        }
        // Keep controller and physical adapter outcomes correlated to this
        // single request; the controller decides whether any future retry is legal.
        _controller.applied(_decision.request, result, now);
      }
      climateState(climate.to<JsonObject>());
      ready = _decision.available;
      reason = _decision.reason;
    }
    // Component callbacks can reenter framework request handlers.
    if (_attached) {
      _climate->setState(climate.as<JsonVariantConst>());
      _ready->set(ready);
      _diagnostic->set(reason);
    }
  }

  void climateState(JsonObject root) const {
    const auto now = thermostatNow();
    const bool feedbackFresh = _input.feedback.valid && now >= _input.feedback.sampledAtMs &&
        now - _input.feedback.sampledAtMs <= _config.feedbackFreshnessMs;
    root["mode"] = _config.heat ? "heat" : "off";
    if (!feedbackFresh || _input.feedback.phase == th::Phase::Unknown)
      root["action"] = nullptr;
    else if (_input.feedback.phase == th::Phase::Cooling) root["action"] = "fan";
    else if (_input.feedback.phase == th::Phase::Heating || _input.feedback.phase == th::Phase::Starting)
      root["action"] = "heating";
    else root["action"] = _config.heat ? "idle" : "off";
    root["temperature"] = _config.targetC;
    const auto& sample = _input.temperature;
    const bool valid = sample.valid && now >= sample.sampledAtMs &&
        now - sample.sampledAtMs <= _config.sensorFreshnessMs;
    if (valid) root["currentTemperature"] = sample.celsius;
    else root["currentTemperature"] = nullptr;
    root["available"] = _config.enabled && !_config.shadow && _decision.available && feedbackFresh && valid;
  }

  JsonDocument* state() {
    std::lock_guard guard(_mutex);
    auto* doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    configToJson(root["config"].to<JsonObject>(), _config);
    climateState(root["climate"].to<JsonObject>());
    auto control = root["control"].to<JsonObject>();
    control["ready"] = _decision.available;
    control["attached"] = _attached;
    control["active"] = _config.enabled && !_config.shadow && _decision.available && !_decision.inhibited;
    control["inhibited"] = _decision.inhibited;
    control["reason"] = _decision.reason;
    control["phase"] = th::phaseName(_input.feedback.phase);
    control["strategy"] = th::strategyName(_decision.strategy);
    control["backend"] = _owner->name();
    control["fullShutdownSupported"] = _input.capabilities.nativeStandby;
    control["nativeTemperatureSupported"] = _input.capabilities.nativeTemperature;
    control["flowSupported"] = _input.capabilities.flow;
    control["pending"] = _input.feedback.commandPending ? "pending" : "";
    control["proposedCommand"] = th::commandName(_decision.request.command);
    control["lastCommand"] = _lastCommand;
    control["lastResult"] = _lastResult;
    auto source = root["source"].to<JsonObject>();
    source["uid"] = _config.sensorUid;
    const auto now = thermostatNow();
    source["valid"] = _input.temperature.valid && now >= _input.temperature.sampledAtMs &&
        now - _input.temperature.sampledAtMs <= _config.sensorFreshnessMs;
    if (_input.temperature.valid && now >= _input.temperature.sampledAtMs)
      source["sampledAtMs"] = _input.temperature.sampledAtMs;
    else source["sampledAtMs"] = nullptr;
    sources(root["sources"].to<JsonArray>(), thermostatNow());
    return doc;
  }

  void describeHa(Request& request) {
    std::lock_guard guard(_mutex);
    if (!_attached) return;
    JsonDocument doc;
    auto root = doc.to<JsonObject>();
    root["id"] = _climate->uid();
    root["component"] = "climate";
    root["componentId"] = _climate->id();
    root["has_state"] = true;
    root["accepts_commands"] = true;
    auto stateTopics = root["stateTopicNames"].to<JsonArray>();
    for (const char* topic : {"mode_state_topic", "action_topic", "temperature_state_topic", "current_temperature_topic"})
      stateTopics.add(topic);
    auto commandTopics = root["commandTopicNames"].to<JsonArray>();
    commandTopics.add("mode_command_topic");
    commandTopics.add("temperature_command_topic");
    auto config = root["config"].to<JsonObject>();
    config["name"] = "Room thermostat";
    auto modes = config["modes"].to<JsonArray>();
    modes.add("off"); modes.add("heat");
    config["mode_state_template"] = "{{ value_json['climate']['mode'] }}";
    config["action_template"] = "{{ value_json['climate']['action'] }}";
    config["temperature_state_template"] = "{{ value_json['climate']['temperature'] }}";
    config["current_temperature_template"] = "{{ value_json['climate']['currentTemperature'] }}";
    config["mode_command_template"] = "{\"mode\":\"{{ value }}\"}";
    config["temperature_command_template"] = "{\"temperature\":{{ value }}}";
    config["temperature_unit"] = "C";
    config["min_temp"] = -20;
    config["max_temp"] = 40;
    config["temp_step"] = 0.5;
    config["precision"] = 0.1;
    config["optimistic"] = false;
    config["availability_mode"] = "all";
    auto availability = config["availability"].to<JsonArray>();
    const auto& mqtt = net::Mqtt::instance();
    const auto& birth = mqtt.getBirth();
    const auto& lwt = mqtt.getLwt();
    if (birth.valid() && lwt.valid() && birth.topic == lwt.topic) {
      auto host = availability.add<JsonObject>();
      host["topic"] = birth.topic;
      host["payload_available"] = birth.payload;
      host["payload_not_available"] = lwt.payload;
    }
    auto readiness = availability.add<JsonObject>();
    readiness["topic"] = std::string("esp32m/") + App::instance().hostname() + "/" + _owner->name() + "/state";
    readiness["value_template"] = "{{ 'online' if value_json['climate']['available'] else 'offline' }}";
    readiness["payload_available"] = "online";
    readiness["payload_not_available"] = "offline";
    request.respond(_owner->name(), doc.as<JsonVariantConst>());
    integrations::ha::DescribeRequest::autoRespond(request, *_ready);
    integrations::ha::DescribeRequest::autoRespond(request, *_diagnostic);
  }

 private:
  Thermostat* _owner;
  std::unique_ptr<th::Actuator> _ownedActuator;
  th::Actuator* _actuator = nullptr;
  bool _attached = false;
  std::recursive_mutex _mutex;
  th::Config _config;
  th::Controller _controller;
  th::Input _input;
  th::Decision _decision;
  std::optional<ThermostatClimate> _climate;
  std::optional<ThermostatReady> _ready;
  std::optional<Sensor> _diagnostic;
  std::string _lastCommand, _lastResult;
  bool _ownsOutput = false;
  uint64_t _ownedRevision = 0;
};

Thermostat::Thermostat(th::Actuator& actuator, const char* name)
    : _name(name && *name ? name : "thermostat"), _impl(std::make_unique<Impl>(this, &actuator)) {
  setSensorsPollInterval(1000);
  Device::init(_impl->rejected() ? Flags::None : Flags::HasSensors);
}
Thermostat::Thermostat(std::unique_ptr<th::Actuator> actuator, const char* name)
    : _name(name && *name ? name : "thermostat"),
      _impl(std::make_unique<Impl>(this, nullptr, std::move(actuator))) {
  setSensorsPollInterval(1000);
  Device::init(_impl->rejected() ? Flags::None : Flags::HasSensors);
}
Thermostat::~Thermostat() = default;
const JsonVariantConst Thermostat::descriptor() const {
  if (_impl->rejected()) return {};
  static JsonDocument doc;
  if (doc.isNull()) doc.to<JsonArray>().add("thermostat");
  return doc.as<JsonArrayConst>();
}
bool Thermostat::pollSensors() {
  if (!_impl->rejected()) _impl->tick();
  return true;
}
JsonDocument* Thermostat::getState(RequestContext&) {
  return _impl->rejected() ? nullptr : _impl->state();
}
JsonDocument* Thermostat::getDeviceConfig(RequestContext&) {
  return _impl->rejected() ? nullptr : _impl->configuration(true);
}
bool Thermostat::setDeviceConfig(RequestContext& ctx) {
  if (_impl->rejected()) return false;
  bool changed = false;
  if (!ctx.data.is<JsonObjectConst>() || !_impl->configure(ctx.data.as<JsonObjectConst>(), changed, ctx.request.isInternal()))
    ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid thermostat settings");
  return changed;
}
void Thermostat::setState(RequestContext& ctx) {
  if (_impl->rejected()) return;
  bool changed = false;
  if (!ctx.data.is<JsonObjectConst>() || !_impl->command(ctx.data.as<JsonObjectConst>(), changed)) {
    ctx.errors.check(ESP_ERR_INVALID_ARG, "Provide one thermostat mode, temperature or resume action");
    return;
  }
  if (changed) config::Changed::publish(this);
}
bool Thermostat::handleRequest(Request& request) {
  // AppObject routes all objects with the same interactiveName, so returning
  // before its generic state/config handlers is as important as blocking I/O.
  // A failed duplicate must not acknowledge or overwrite its owner's response.
  if (_impl->rejected()) return false;
  if (request.is("available-sensors")) {
    std::unique_ptr<JsonDocument> state(_impl->state());
    request.respond((*state)["sources"].as<JsonVariantConst>(), false);
    return true;
  }
  if (request.is(integrations::ha::DescribeRequest::Name)) {
    _impl->describeHa(request);
    return true;
  }
  if (request.is(integrations::ha::CommandRequest::Name)) {
    auto& command = static_cast<integrations::ha::CommandRequest&>(request);
    if (command.componentId != "climate") return false;
    if (command.retained) { request.respond(ESP_ERR_INVALID_STATE); return true; }
    const auto payload = command.data().as<const char*>();
    JsonDocument doc;
    if (!payload) { request.respond(ESP_ERR_INVALID_ARG); return true; }
    auto error = deserializeJson(doc, payload);
    if (error) {
      if (!strcmp(payload, "off") || !strcmp(payload, "heat")) doc["mode"] = payload;
      else { request.respond(ESP_ERR_INVALID_ARG); return true; }
    }
    bool changed = false;
    if (!doc.is<JsonObjectConst>() || !_impl->command(doc.as<JsonObjectConst>(), changed)) {
      request.respond(ESP_ERR_INVALID_ARG);
      return true;
    }
    if (changed) config::Changed::publish(this);
    request.respond();
    return true;
  }
  return Device::handleRequest(request);
}
Thermostat* useThermostat(th::Actuator& actuator, const char* name) { return new Thermostat(actuator, name); }
Thermostat* useThermostat(std::unique_ptr<th::Actuator> actuator, const char* name) {
  return new Thermostat(std::move(actuator), name);
}
}  // namespace esp32m::dev
