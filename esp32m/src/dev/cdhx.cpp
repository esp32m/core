#include "esp32m/dev/cdhx.hpp"
#include "esp32m/dev/cdhx/protocol.hpp"
#include "esp32m/config/config.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/events/response.hpp"

#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <esp_task_wdt.h>
#include <freertos/queue.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <mutex>

// Temporary laboratory extensions belong to the opting-in test application.
// Ordinary consumers have no research code, storage, or link dependency.
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
#include "engine.hpp"
#include "driver-access.hpp"
#endif

namespace esp32m::dev {
namespace {
namespace p = cdhx::protocol;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
namespace research = ::cdhx_test::research;
#endif
uint64_t nowUs() { return static_cast<uint64_t>(esp_timer_get_time()); }
const char* modeName(uint8_t mode) {
  return mode == p::ThermostatMode ? "temperature" :
      mode == p::FixedPowerMode ? "power" : "unknown";
}
const char* stateName(uint8_t value) {
  static const char* names[] = {"standby", "start-acknowledged", "preheat",
      "ignition-retry", "heating-up", "running", "shutdown-requested",
      "post-glow", "cooldown"};
  return value < std::size(names) ? names[value] : "unknown";
}
const char* statusName(uint8_t value) {
  static const char* names[] = {"idle", "running", "low-voltage", "high-voltage",
      "glow-failure", "pump-failure", "overtemperature", "fan-failure",
      "communication-loss", "flame-out", "temperature-sensor-failure",
      "ignition-retries-exhausted"};
  return value < std::size(names) ? names[value] : "unknown";
}
std::string hex(const p::Frame& frame) {
  char buffer[p::FrameSize * 3]{};
  for (size_t i = 0; i < frame.size(); ++i)
    std::snprintf(buffer + i * 3, sizeof(buffer) - i * 3, "%02X%s",
                  frame[i], i + 1 == frame.size() ? "" : " ");
  return buffer;
}
void writeService(JsonObject object, const p::ServiceParameters& service) {
  object["minimumPumpHz"] = service.minimumPumpDecihz / 10.0;
  object["maximumPumpHz"] = service.maximumPumpDecihz / 10.0;
  object["minimumFanRpm"] = service.minimumFanRpm;
  object["maximumFanRpm"] = service.maximumFanRpm;
  object["configuredVoltageV"] = service.configuredVoltageDeciv / 10.0;
  object["hallCount"] = service.fanHallPulses;
  object["glowPowerLevel"] = service.glowPower;
}
bool readService(JsonVariantConst value, p::ServiceParameters& service) {
  if (!value.is<JsonObjectConst>() || value.size() != 7) return false;
  const auto object = value.as<JsonObjectConst>();
  const char* keys[] = {"minimumPumpHz", "maximumPumpHz", "minimumFanRpm",
      "maximumFanRpm", "configuredVoltageV", "hallCount", "glowPowerLevel"};
  for (const auto member : object) {
    bool known = false;
    for (const auto key : keys)
      if (member.key().size() == std::strlen(key) && !std::strcmp(member.key().c_str(), key))
        known = true;
    if (!known) return false;
  }
  auto deci = [](JsonVariantConst input, int& result) {
    if (!input.is<double>()) return false;
    const double scaled = input.as<double>() * 10;
    if (!std::isfinite(scaled) || scaled < 0 || scaled > 255 ||
        std::abs(scaled - std::round(scaled)) > 0.000001) return false;
    result = static_cast<int>(std::round(scaled));
    return true;
  };
  p::ServiceParameters result;
  if (!deci(object["minimumPumpHz"], result.minimumPumpDecihz) ||
      !deci(object["maximumPumpHz"], result.maximumPumpDecihz) ||
      !deci(object["configuredVoltageV"], result.configuredVoltageDeciv) ||
      !object["minimumFanRpm"].is<int>() || !object["maximumFanRpm"].is<int>() ||
      !object["hallCount"].is<int>() || !object["glowPowerLevel"].is<int>()) return false;
  result.minimumFanRpm = object["minimumFanRpm"].as<int>();
  result.maximumFanRpm = object["maximumFanRpm"].as<int>();
  result.fanHallPulses = object["hallCount"].as<int>();
  result.glowPower = object["glowPowerLevel"].as<int>();
  if (!p::validService(result)) return false;
  service = result;
  return true;
}
class ConfigJobRequest : public Request {
 public:
  ConfigJobRequest(const char* target, JsonVariantConst data)
      : Request("config-set", 0, target, data, "cdhx/config") {}
 protected:
  void respondImpl(const char*, JsonVariantConst, bool) override {}
};
}  // namespace

class Cdhx::Impl {
 public:
  Impl(Cdhx* owner, const Options& options)
      : _options(options), _model(options.model ? options.model : "CDHX1 / H45214A0"),
        _sides{Side(options.display), Side(options.heater)},
        _voltage(owner, "voltage", "supply"), _fan(owner, "", "fan_rpm"),
        _body(owner, "temperature", "body"), _ambient(owner, "temperature", "ambient"),
        _pump(owner, "", "pump_setting"), _statusSensor(owner, "", "status") {
    std::array<uint8_t, 16> identity{};
    esp_fill_random(identity.data(), identity.size());
    char boot[33]{};
    for (size_t i = 0; i < identity.size(); ++i)
      std::snprintf(boot + i * 2, sizeof(boot) - i * 2, "%02x", identity[i]);
    _configurationBootId = boot;
    _voltage.precision = _pump.precision = 1;
    _fan.precision = _body.precision = _ambient.precision = _statusSensor.precision = 0;
    _fan.setTitle("Fan speed (RPM)");
    _body.setTitle("Reported heat-exchanger temperature");
    _ambient.setTitle("Physical display ambient temperature");
    _pump.setTitle("Enabled base-pump setting (Hz)");
    _statusSensor.setTitle("Heater status code");
  }

  ~Impl() {
    stopConfigWorker();
    // Quiesce callbacks before deleting their destination task. A callback may
    // otherwise hold a task handle concurrently with the worker's final exit.
    if (_wakeTimer) esp_timer_stop_blocking(_wakeTimer, portMAX_DELAY);
    _stop.store(true);
    // Cooperative shutdown: never delete a task while it owns the mutex.
    while (_task.load()) vTaskDelay(1);
    if (_wakeTimer) {
      esp_timer_delete(_wakeTimer);
    }
    if (_hardwareValidated) {
      releaseOutputs();
      selectBypass();
      if (hasBypass()) gpio_reset_pin(_options.bypassPin);
    }
    for (auto& side : _sides)
      if (side.installed) uart_driver_delete(side.hw.uart);
  }

  esp_err_t start() {
    auto validEndpoint = [](const Endpoint& hw) {
      return hw.uart >= UART_NUM_0 && hw.uart < UART_NUM_MAX &&
             GPIO_IS_VALID_GPIO(hw.rxPin) && GPIO_IS_VALID_OUTPUT_GPIO(hw.txPin);
    };
    if (!validEndpoint(_options.heater) || !validEndpoint(_options.display) ||
        _options.heater.uart == _options.display.uart ||
        _options.freshnessMs < 500 || _options.freshnessMs > 60000 ||
        _options.responseTimeoutMs < 50 || _options.responseTimeoutMs > 1000 ||
        _options.quietBeforeArmMs < 20 || _options.quietBeforeArmMs > 200 ||
        _options.responseTimeoutMs >= _options.freshnessMs ||
        _options.interceptLevel < 0 || _options.interceptLevel > 1 ||
        _options.bypassSettleMs < 1 || _options.bypassSettleMs > 100 ||
        (hasBypass() && !GPIO_IS_VALID_OUTPUT_GPIO(_options.bypassPin)) ||
        !validPowerRange() || (_options.requireFixedPowerTemplate &&
          !p::validRequest(_options.fixedPowerTemplate)))
      return startupFailure(ESP_ERR_INVALID_ARG);
    const std::array<gpio_num_t, 4> pins{_options.heater.rxPin, _options.heater.txPin,
        _options.display.rxPin, _options.display.txPin};
    for (size_t i = 0; i < pins.size(); ++i)
      if (hasBypass() && pins[i] == _options.bypassPin)
        return startupFailure(ESP_ERR_INVALID_ARG);
    for (size_t i = 0; i < pins.size(); ++i)
      for (size_t j = i + 1; j < pins.size(); ++j)
        if (pins[i] == pins[j]) return startupFailure(ESP_ERR_INVALID_ARG);
    // Do not alter pins belonging to a pre-existing UART driver.
    for (auto& side : _sides)
      if (uart_is_driver_installed(side.hw.uart))
        return startupFailure(ESP_ERR_INVALID_STATE);
    _hardwareValidated = true;
    releaseOutputs();
    selectBypass();
    esp_timer_create_args_t timer{};
    timer.callback = [](void* context) {
      auto* self = static_cast<Impl*>(context);
      if (auto task = self->_task.load()) xTaskNotifyGive(task);
    };
    timer.arg = this;
    timer.name = "cdhx-wake";
    auto error = esp_timer_create(&timer, &_wakeTimer);
    if (error != ESP_OK) return startupFailure(error);
    TaskHandle_t task = nullptr;
    // UART ISR allocation follows its installing task's core. Keep both UARTs
    // and their owner away from the default Wi-Fi/main core on dual-core chips.
    constexpr BaseType_t core = CONFIG_FREERTOS_NUMBER_OF_CORES > 1 ? 1 : 0;
    if (xTaskCreatePinnedToCore(taskEntry, "cdhx", 6144, this, 20, &task, core) != pdPASS)
      return startupFailure(ESP_ERR_NO_MEM);
    _task.store(task);
    while (!_startupDone.load()) vTaskDelay(1);
    return _startupResult;
  }

  esp_err_t initializeHardware() {
    for (auto& side : _sides) {
      uart_config_t config{};
      config.baud_rate = p::BaudRate;
      config.data_bits = UART_DATA_8_BITS;
      config.parity = UART_PARITY_DISABLE;
      config.stop_bits = UART_STOP_BITS_1;
      config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
      config.source_clk = UART_SCLK_DEFAULT;
      auto error = uart_param_config(side.hw.uart, &config);
      if (error == ESP_OK) {
        error = uart_driver_install(side.hw.uart, 2048, 0, 64, &side.events, 0);
        side.installed = error == ESP_OK;
      }
      if (error == ESP_OK)
        error = uart_set_pin(side.hw.uart, UART_PIN_NO_CHANGE, side.hw.rxPin,
                              UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
      if (error == ESP_OK) error = uart_set_rx_full_threshold(side.hw.uart, 1);
      if (error == ESP_OK) error = uart_set_rx_timeout(side.hw.uart, 1);
      if (error != ESP_OK) return startupFailure(error);
    }
    auto error = esp_timer_start_periodic(_wakeTimer, 1000);
    return error == ESP_OK ? ESP_OK : startupFailure(error);
  }

  esp_err_t turn(bool on) {
    std::lock_guard lock(_mutex);
    if (_thermostatCommand || _thermostatOverride) manualTakeover();
    const auto result = queueTurn(on);
    if (result == ESP_OK) manualTakeover(false);
    return result;
  }
  esp_err_t queueTurn(bool on) {
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    if (research::engine().blocksLegacyControl()) return ESP_ERR_INVALID_STATE;
#endif
    if (!ready(nowUs()) || _pendingCommand || _observingCommand)
      return ESP_ERR_INVALID_STATE;
    if (on ? (_response.runState != 0 || _response.status > 1)
           : (_response.runState < 1 || _response.runState > 5))
      return ESP_ERR_INVALID_STATE;
    _pendingCommand = on ? p::StartCommand : p::StopCommand;
    _lastCommand = on ? "start" : "stop";
    _lastCommandResult = "queued";
    ++_configurationRevision;
    return ESP_OK;
  }

  esp_err_t setTemperature(int value) {
    std::lock_guard lock(_mutex);
    const auto result = acceptSetting(Mode::Temperature, value);
    if (result == ESP_OK) manualTakeover();
    return result;
  }
  esp_err_t setFixedPower(int raw) {
    std::lock_guard lock(_mutex);
    const auto result = acceptSetting(Mode::FixedPower, raw);
    if (result == ESP_OK) manualTakeover();
    return result;
  }
  esp_err_t setConfiguration(const Configuration& configuration) {
    std::lock_guard lock(_mutex);
    const auto result = acceptSetting(configuration.mode, configuration.value);
    if (result == ESP_OK) manualTakeover();
    return result;
  }
  thermostat::Capabilities thermostatCapabilities() const {
    std::lock_guard lock(_mutex);
    thermostat::Capabilities result;
    result.startStop = true;
    const auto mode = _thermostatOverride ? _thermostatMode :
        (_operatingConfigured ? _configuration.mode :
         (_request.fixedPower() ? Mode::FixedPower : Mode::Temperature));
    result.nativeTarget = mode == Mode::Temperature && _request.temperatureWritable();
    result.nativeTemperature = result.nativeTarget && _request.ambientTemperatureC >= 0;
    result.power = mode == Mode::FixedPower && powerQualified();
    return result;
  }
  thermostat::Feedback thermostatSnapshot() const {
    std::lock_guard lock(_mutex);
    const auto stamp = nowUs();
    thermostat::Feedback result;
    result.revision = _manualRevision;
    result.sampledAtMs = _heaterStamp / 1000;
    result.valid = ready(stamp) && _paired;
    result.enabled = !_bypassRequested;
    result.fault = !_fault.empty() || (_heaterStamp && _response.status > 1);
    result.commandPending = _pendingCommand || _observingCommand;
    result.commandUncertain = _lastCommandResult == "uncertain-no-retry" ||
        _lastCommandResult == "outcome-unconfirmed-no-retry";
    if (result.fault) result.phase = thermostat::Phase::Fault;
    else if (!result.valid) result.phase = thermostat::Phase::Unknown;
    else if (fullyStopped()) result.phase = thermostat::Phase::Off;
    else if (_response.runState >= 1 && _response.runState <= 4)
      result.phase = thermostat::Phase::Starting;
    else if (_response.runState == 5) result.phase = thermostat::Phase::Heating;
    else result.phase = thermostat::Phase::Cooling;
    result.minimumKnown = _options.thermostatLowPumpDecihz > 0 &&
        _options.thermostatLowPumpDecihz <= 255;
    result.atMinimum = result.valid && _response.runState == 5 && _response.status == 1 &&
        result.minimumKnown && _response.pumpDecihz > 0 &&
        _response.pumpDecihz <= _options.thermostatLowPumpDecihz;
    const auto mode = _operatingConfigured ? _configuration.mode :
        (_request.fixedPower() ? Mode::FixedPower : Mode::Temperature);
    result.nativeTemperature = {float(_request.ambientTemperatureC), _displayStamp / 1000,
        fresh(_displayStamp, stamp) && _request.thermostat() &&
        mode == Mode::Temperature && _request.ambientTemperatureC >= 0};
    return result;
  }
  thermostat::Result thermostatApply(const thermostat::Request& request) {
    using namespace thermostat;
    std::lock_guard lock(_mutex);
    if (request.expectedRevision != _manualRevision) return Result::Rejected;
    if (request.command == Command::Release) {
      _thermostatOverride = false;
      if (_thermostatCommand && _pendingCommand) cancelRemote();
      _thermostatCommand = false;
      return Result::Accepted;
    }
    if (!request.validUntilMs || nowUs() / 1000 >= request.validUntilMs) return Result::Rejected;
    if (!ready(nowUs())) return Result::Unavailable;
    if (_pendingCommand || _observingCommand) return Result::Busy;
    if (_lastCommandResult == "uncertain-no-retry" ||
        _lastCommandResult == "outcome-unconfirmed-no-retry") return Result::Rejected;
    if (request.command == Command::Start || request.command == Command::Stop) {
      if (request.command == Command::Start && !fullyStopped()) return Result::Busy;
      const auto error = queueTurn(request.command == Command::Start);
      if (error != ESP_OK) return Result::Unavailable;
      _thermostatCommand = true;
      _thermostatUntilMs = request.validUntilMs;
      return Result::Accepted;
    }
    const auto mode = _operatingConfigured ? _configuration.mode :
        (_request.fixedPower() ? Mode::FixedPower : Mode::Temperature);
    if (request.command == Command::NativeTarget) {
      // Remote ambient substitution is deliberately unavailable until qualified.
      if (std::isfinite(request.ambientC)) return Result::Unsupported;
      if (mode != Mode::Temperature || !_request.temperatureWritable()) return Result::Unsupported;
      if (!std::isfinite(request.targetC) || request.targetC < _request.minimumTemperatureC ||
          request.targetC > _request.maximumTemperatureC) return Result::Invalid;
      _thermostatMode = mode;
      _thermostatValue = static_cast<int>(std::lround(request.targetC));
    } else if (request.command == Command::Power) {
      if (mode != Mode::FixedPower || !powerQualified()) return Result::Unsupported;
      if (!std::isfinite(request.powerFraction) || request.powerFraction < 0 || request.powerFraction > 1)
        return Result::Invalid;
      _thermostatMode = mode;
      _thermostatValue = _options.fixedPowerMinimumRaw + static_cast<int>(std::lround(
          request.powerFraction * (_options.fixedPowerMaximumRaw - _options.fixedPowerMinimumRaw)));
    } else return Result::Unsupported;
    _thermostatOverride = true;
    _thermostatUntilMs = request.validUntilMs;
    return Result::Accepted;
  }
  JsonDocument* serviceConfig() {
    std::lock_guard lock(_mutex);
    return serviceConfigLocked();
  }
  JsonDocument* serviceConfigLocked() {
    auto* doc = new JsonDocument();
    if (_serviceConfigured) writeService((*doc)["service"].to<JsonObject>(), _service);
    else doc->to<JsonObject>();
    if (_operatingConfigured) {
      auto operating = (*doc)["operating"].to<JsonObject>();
      operating["mode"] = modeName(configurationMode(_configuration));
      if (_temperatureSetting >= 0) operating["temperatureSetpointC"] = _temperatureSetting;
      if (_powerSetting >= 0) operating["fixedPowerSettingRaw"] = _powerSetting;
    }
    return doc;
  }
  bool setServiceConfig(RequestContext& ctx, uint64_t* revision = nullptr,
                        uint64_t* responses = nullptr, uint64_t* interruption = nullptr) {
    const bool internal = ctx.request.isInternal();
    if (!ctx.data.is<JsonObjectConst>()) {
      ctx.errors.add("Service configuration must be an object");
      return false;
    }
    if (internal && !ctx.data.size()) return false;
    for (const auto member : ctx.data.as<JsonObjectConst>()) {
      const char* key = member.key().c_str();
      if (std::strlen(key) != member.key().size() ||
          (std::strcmp(key, "service") && std::strcmp(key, "operating") &&
           (internal || (std::strcmp(key, "expectedBootId") &&
            std::strcmp(key, "expectedRevision") && std::strcmp(key, "acceptBeforeUptimeMs"))))) {
        ctx.errors.add("Unknown service configuration field");
        return false;
      }
    }
    const bool hasService = !ctx.data["service"].isUnbound();
    const bool hasOperating = !ctx.data["operating"].isUnbound();
    if (!hasService && !hasOperating) {
      ctx.errors.add("Provide operating or service settings");
      return false;
    }
    p::ServiceParameters service;
    if (hasService && !readService(ctx.data["service"], service)) {
      ctx.errors.add("Provide all seven service values within their supported ranges and steps");
      return false;
    }
    std::lock_guard lock(_mutex);
    Mode mode = _configuration.mode;
    int temperature = _temperatureSetting, power = _powerSetting;
    if (hasOperating) {
      const auto input = ctx.data["operating"];
      if (!input.is<JsonObjectConst>() || !input["mode"].is<const char*>()) {
        ctx.errors.add("Operating settings require a mode and selected integer setpoint");
        return false;
      }
      for (const auto member : input.as<JsonObjectConst>()) {
        const char* key = member.key().c_str();
        if (std::strlen(key) != member.key().size() ||
            (std::strcmp(key, "mode") && std::strcmp(key, "temperatureSetpointC") &&
             std::strcmp(key, "fixedPowerSettingRaw"))) {
          ctx.errors.add("Unknown operating settings field");
          return false;
        }
      }
      const auto selected = input["mode"].as<std::string>();
      if (selected == "temperature") mode = Mode::Temperature;
      else if (selected == "power") mode = Mode::FixedPower;
      else { ctx.errors.add("Invalid operating mode"); return false; }
      if (!input["temperatureSetpointC"].isUnbound()) {
        if (!input["temperatureSetpointC"].is<int>() || input["temperatureSetpointC"].as<int>() < 0 ||
            input["temperatureSetpointC"].as<int>() > 127) {
          ctx.errors.add("Invalid stored temperature setpoint"); return false;
        }
        temperature = input["temperatureSetpointC"].as<int>();
      }
      if (!input["fixedPowerSettingRaw"].isUnbound()) {
        if (!input["fixedPowerSettingRaw"].is<int>() || input["fixedPowerSettingRaw"].as<int>() < 0 ||
            input["fixedPowerSettingRaw"].as<int>() > 255) {
          ctx.errors.add("Invalid stored power setpoint"); return false;
        }
        power = input["fixedPowerSettingRaw"].as<int>();
      }
      if ((mode == Mode::Temperature && !input["temperatureSetpointC"].is<int>()) ||
          (mode == Mode::FixedPower && !input["fixedPowerSettingRaw"].is<int>())) {
        ctx.errors.add("Provide the selected mode's setpoint"); return false;
      }
    }
    if (!internal) {
      const auto stamp = nowUs();
      if (ctx.request.isBroadcast()) {
        ctx.errors.add("Configuration requires an explicit target");
        return false;
      }
      if (hasService) if (const char* reason = serviceReason(stamp)) {
        ctx.errors.add(reason);
        return false;
      }
      if (hasOperating) {
        Configuration candidate;
        candidate.mode = mode;
        candidate.value = mode == Mode::Temperature ? temperature : power;
        if (const char* reason = configurationReason(candidate, stamp, true)) {
          ctx.errors.add(reason); return false;
        }
        auto patched = _stockFrame;
        if (hasService) p::patchService(patched, service);
        if (!p::patchOperating(patched, configurationMode(candidate), candidate.value, _request.ambientTemperatureC) ||
            (!inputPowerValid(ctx.data["operating"], power, patched)) ||
            (!ctx.data["operating"]["temperatureSetpointC"].isUnbound() &&
             (temperature < _request.minimumTemperatureC || temperature > _request.maximumTemperatureC))) {
          ctx.errors.add("Operating value is outside the qualified range"); return false;
        }
      }
      manualTakeover();
    }
    bool changed = hasService && (!_serviceConfigured || !(_service == service));
    if (hasService) { _service = service; _serviceConfigured = true; }
    if (hasOperating) {
      changed |= !_operatingConfigured || mode != _configuration.mode ||
          temperature != _temperatureSetting || power != _powerSetting;
      _temperatureSetting = temperature;
      _powerSetting = power;
      _configuration.mode = mode;
      _configuration.value = mode == Mode::Temperature ? temperature : power;
      _operatingConfigured = true;
      _configurationActive = _configurationFlight = false;
      _configurationPhase = "pending";
      _configurationEndedReason = nullptr;
    }
    if (responses) *responses = _forwardedResponses;
    if (interruption) *interruption = _transportInterruption;
    if (!changed) {
      if (revision) *revision = _settingsRevision;
      return false;
    }
    _serviceSavePending = false; // config-set itself publishes Changed after this lock.
    ++_configurationRevision;
    ++_settingsRevision;
    if (revision) *revision = _settingsRevision;
    return true;
  }
  void stopConfigWorker() {
    _configStop.store(true);
    while (_configWorkerRunning.load()) vTaskDelay(1);
  }
  void startConfigWorker(Cdhx* owner, Request& request) {
    bool busy;
    {
      std::lock_guard lock(_mutex);
      busy = _configStop.load() || _configWorkerRunning.exchange(true);
    }
    if (busy) {
      request.respondError(ESP_ERR_INVALID_STATE, "A configuration update is already pending");
      return;
    }
    auto response = std::unique_ptr<Response>(request.makeResponse());
    if (!response) {
      request.respond(ESP_ERR_NO_MEM);
      _configWorkerRunning.store(false);
      return;
    }
    auto* job = new ConfigJob{this, owner, std::move(response)};
    const auto created = xTaskCreate([](void* argument) {
      std::unique_ptr<ConfigJob> job(static_cast<ConfigJob*>(argument));
      auto* self = job->self;
      self->runConfigJob(*job);
      job.reset();
      self->_configWorkerRunning.store(false);
      vTaskDelete(nullptr);
    }, "m/cdhx/config", 6144, job, 1, nullptr);
    if (created != pdPASS) {
      job->response->setError(ESP_ERR_NO_MEM);
      job->response->publish();
      delete job;
      _configWorkerRunning.store(false);
    }
  }
  bool takeServiceSavePending() {
    std::lock_guard lock(_mutex);
    if (!App::initialized() || !_serviceSavePending) return false;
    _serviceSavePending = false;
    return true;
  }
  esp_err_t releaseOverride() {
    std::lock_guard lock(_mutex);
    manualTakeover();
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    research::engine().release("explicit-release", nowUs());
#endif
    return ESP_OK;
  }
  esp_err_t resetBridge() {
    std::lock_guard lock(_mutex);
    if (!_task.load() || _active || _fault.empty()) return ESP_ERR_INVALID_STATE;
    manualTakeover();
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    research::engine().release("bridge-reset", nowUs());
#endif
    _bypassRequested = false;
    _resetRequested = true;
    return ESP_OK;
  }
  esp_err_t setBypass(bool enabled) {
    std::lock_guard lock(_mutex);
    if (!hasBypass() || !_task.load() || (!enabled && !_fault.empty()))
      return ESP_ERR_INVALID_STATE;
    manualTakeover();
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    research::engine().release("bypass-change", nowUs());
#endif
    const bool hadPending = _pendingCommand;
    const bool hadTransmitted = _observingCommand;
    ++_transportInterruption;
    releaseOutputs();
    selectBypass();
    _active = _waitingResponse = false;
    _paired = false;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    _researchPaired = false;
#endif
    cancelRemote();
    if (hadTransmitted) _lastCommandResult = "outcome-unconfirmed-no-retry";
    else if (hadPending) _lastCommandResult = "cancelled-by-bypass";
    _bypassRequested = enabled;
    _resetRequested = !enabled;
    _sensorDirty = true;
    clearMirrors();
    _displayStamp = _heaterStamp = 0;
    for (auto& side : _sides) {
      side.echoPending = 0;
      side.parser.reset();
      uart_flush_input(side.hw.uart);
      xQueueReset(side.events);
      side.lastRx = nowUs();
    }
    return ESP_OK;
  }

  void toState(JsonObject root) {
    std::lock_guard lock(_mutex);
    auto stamp = nowUs();
    const bool displayFresh = fresh(_displayStamp, stamp), heaterFresh = fresh(_heaterStamp, stamp);
    root["connected"] = _active && displayFresh && heaterFresh;
    root["bridgeActive"] = _active;
    root["model"] = _model;
    if (_fault.empty()) root["fault"] = nullptr; else root["fault"] = _fault;
    auto bypass = root["bypass"].to<JsonObject>();
    bypass["supported"] = hasBypass();
    bypass["active"] = hasBypass() && _bypassActive;
    bypass["requested"] = _bypassRequested;
    bypass["qualifiedPairs"] = _qualifiedPairs;
    auto display = root["display"].to<JsonObject>();
    display["valid"] = displayFresh;
    display["sampledAtMs"] = _displayStamp / 1000;
    display["mode"] = modeName(_request.mode);
    display["ambientC"] = _request.ambientTemperatureC;
    display["requestedRaw"] = _request.requestedValueRaw;
    if (_request.thermostat()) display["temperatureSetpointC"] = _request.requestedValueRaw;
    display["minimumTemperatureC"] = _request.minimumTemperatureC;
    display["maximumTemperatureC"] = _request.maximumTemperatureC;
    display["requestHex"] = _displayStamp ? hex(_stockFrame) : "";
    auto control = root["control"].to<JsonObject>();
    control["available"] = ready(stamp) && !_pendingCommand && !_observingCommand;
    if (_operatingConfigured && _configuration.mode == Mode::Temperature)
      control["temperatureOverrideC"] = _temperatureSetting;
    else control["temperatureOverrideC"] = nullptr;
    if (_operatingConfigured && _configuration.mode == Mode::FixedPower)
      control["fixedPowerOverrideRaw"] = _powerSetting;
    else control["fixedPowerOverrideRaw"] = nullptr;
    if (_options.fixedPowerMinimumRaw < 0) {
      control["fixedPowerMinimumRaw"] = nullptr;
      control["fixedPowerMaximumRaw"] = nullptr;
    } else {
      control["fixedPowerMinimumRaw"] = _options.fixedPowerMinimumRaw;
      control["fixedPowerMaximumRaw"] = _options.fixedPowerMaximumRaw;
    }
    if (_pendingCommand) control["pendingCommand"] = _pendingCommand == p::StartCommand ? "start" : "stop";
    else control["pendingCommand"] = nullptr;
    control["lastCommand"] = _lastCommand;
    control["lastCommandResult"] = _lastCommandResult;
    if (_hasSubmission) {
      control["emittedRequestHex"] = hex(_emittedFrame);
      control["emittedRequestedRaw"] = _emittedFrame[4];
    }
    auto configuration = root["configuration"].to<JsonObject>();
    configuration["bootId"] = _configurationBootId;
    configuration["revision"] = _configurationRevision;
    configuration["updatePending"] = _configWorkerRunning.load();
    configuration["active"] = _configurationActive && _active && displayFresh;
    configuration["authority"] = _operatingConfigured;
    if (_temperatureSetting >= 0) configuration["temperatureSetpointC"] = _temperatureSetting;
    else configuration["temperatureSetpointC"] = nullptr;
    if (_powerSetting >= 0) configuration["fixedPowerSettingRaw"] = _powerSetting;
    else configuration["fixedPowerSettingRaw"] = nullptr;
    configuration["stockMode"] = modeName(_request.mode);
    configuration["submittedMode"] = _hasSubmission ? modeName(_emittedFrame[13]) : "unknown";
    if (_hasSubmission) configuration["submittedValue"] = _emittedFrame[4];
    else configuration["submittedValue"] = nullptr;
    if (_operatingConfigured) {
      configuration["mode"] = modeName(configurationMode(_configuration));
      configuration["value"] = _configuration.value;
    } else {
      configuration["mode"] = nullptr;
      configuration["value"] = nullptr;
    }
    Configuration candidate;
    candidate.mode = _operatingConfigured ? _configuration.mode :
        (_request.fixedPower() ? Mode::FixedPower : Mode::Temperature);
    candidate.value = _operatingConfigured ? _configuration.value : _request.requestedValueRaw;
    const char* reason = configurationReason(candidate, stamp, true);
    candidate.mode = candidate.mode == Mode::Temperature ? Mode::FixedPower : Mode::Temperature;
    if (!configurationReason(candidate, stamp, true)) reason = nullptr;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    if (research::engine().blocksLegacyControl()) reason = "Research owns heater control";
#endif
    configuration["canApply"] = !reason;
    configuration["modeChangeAvailable"] = !reason && fullyStopped();
    configuration["ambientAvailable"] = displayFresh && _request.thermostat() &&
        _request.ambientTemperatureC >= 0;
    configuration["powerAvailable"] = powerQualified();
    configuration["phase"] = _configurationPhase;
    if (reason) configuration["reason"] = reason;
    else if (_configurationEndedReason) configuration["reason"] = _configurationEndedReason;
    else configuration["reason"] = nullptr;
    const char* serviceBlocked = serviceReason(stamp);
    configuration["serviceAuthority"] = _serviceConfigured;
    configuration["serviceWritable"] = true;
    configuration["serviceCanApply"] = !serviceBlocked;
    if (serviceBlocked) configuration["serviceReason"] = serviceBlocked;
    else configuration["serviceReason"] = nullptr;
    writeService(configuration["service"].to<JsonObject>(),
        _serviceConfigured ? _service : p::serviceParameters(_request));
    if (_displayStamp)
      writeService(configuration["stockService"].to<JsonObject>(), p::serviceParameters(_request));
    if (_hasSubmission) {
      p::Request submitted;
      if (p::decodeRequest(_emittedFrame, submitted))
        writeService(configuration["submittedService"].to<JsonObject>(), p::serviceParameters(submitted));
    }
    auto heater = root["heater"].to<JsonObject>();
    heater["valid"] = heaterFresh;
    heater["sampledAtMs"] = _heaterStamp / 1000;
    heater["state"] = _response.runState;
    heater["stateName"] = stateName(_response.runState);
    heater["status"] = _response.status;
    heater["statusName"] = statusName(_response.status);
    heater["supplyVoltageV"] = _response.supplyVoltageDeciv / 10.0;
    heater["fanRpm"] = _response.fanRpm;
    heater["fanVoltageV"] = _response.fanVoltageDeciv / 10.0;
    heater["bodyTemperatureC"] = _response.bodyTemperatureC;
    heater["glowVoltageV"] = _response.glowVoltageDeciv / 10.0;
    heater["glowCurrentA"] = _response.glowCurrentCentia / 100.0;
    heater["pumpSettingHz"] = _response.pumpDecihz / 10.0;
    heater["retainedConfigByte"] = _response.retained17;
    heater["identityFailures"] = _response.auxiliary18;
    heater["settingFeedbackRaw"] = _response.feedbackRaw;
    heater["constant20"] = _response.constant20;
    heater["reserved21"] = _response.reserved21;
    heater["responseHex"] = _heaterStamp ? hex(_heaterFrame) : "";
    auto serial = root["serial"].to<JsonObject>();
    serial["rxDisplay"] = _sides[0].rxBytes;
    serial["rxHeater"] = _sides[1].rxBytes;
    serial["forwardedRequests"] = _forwardedRequests;
    serial["forwardedResponses"] = _forwardedResponses;
    serial["crcErrors"] = _sides[0].parser.stats().crcErrors + _sides[1].parser.stats().crcErrors;
    serial["framingErrors"] = _framingErrors;
    serial["echoErrors"] = _echoErrors;
    serial["echoDelayedReads"] = _echoDelayedReads;
    serial["echoMaxServiceDelayUs"] = _echoMaxServiceDelayUs;
    serial["timeouts"] = _timeouts;
    serial["lastError"] = _lastError;
    serial["workerMaxGapUs"] = _workerMaxGapUs;
    serial["workerMaxStepUs"] = _workerMaxStepUs;
    serial["rtosTickUs"] = 1000000 / CONFIG_FREERTOS_HZ;
    if (_faultDetail.kind) {
      auto detail = serial["faultDetail"].to<JsonObject>();
      detail["side"] = _faultDetail.side == 0 ? "display" : "heater";
      detail["kind"] = _faultDetail.kind;
      detail["atUs"] = _faultDetail.atUs;
      if (_faultDetail.expectedByte < 0) detail["expectedByte"] = nullptr;
      else detail["expectedByte"] = _faultDetail.expectedByte;
      if (_faultDetail.receivedByte < 0) detail["receivedByte"] = nullptr;
      else detail["receivedByte"] = _faultDetail.receivedByte;
      detail["echoPosition"] = _faultDetail.echoPosition;
      detail["echoPending"] = _faultDetail.echoPending;
      detail["echoDeadlineUs"] = _faultDetail.echoDeadlineUs;
      detail["bufferedRx"] = _faultDetail.bufferedRx;
      detail["queuedEvents"] = _faultDetail.queuedEvents;
      detail["rxLevel"] = _faultDetail.rxLevel;
      detail["workerGapUs"] = _faultDetail.workerGapUs;
      detail["stepAgeUs"] = _faultDetail.stepAgeUs;
      detail["rxReadAtUs"] = _faultDetail.rxReadAtUs;
      detail["rxReadBytes"] = _faultDetail.rxReadBytes;
    }
  }

 private:
  friend class Cdhx;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
  bool _researchPaired = false;
#endif
  struct Side {
    Endpoint hw;
    p::Parser parser;
    QueueHandle_t events = nullptr;
    bool installed = false;
    p::Frame echo{};
    size_t echoPosition = 0, echoPending = 0;
    uint64_t echoDeadline = 0, lastRx = 0, rxBytes = 0, readObservedAt = 0;
    int readCount = 0;
    std::array<p::Frame, 8> mirrors{};
    size_t mirrorHead = 0, mirrorCount = 0;
    explicit Side(Endpoint endpoint) : hw(endpoint) {}
  };
  Options _options;
  std::string _model;
  std::array<Side, 2> _sides; // display, heater; indexes are physical direction.
  mutable std::mutex _mutex;
  uint64_t _manualRevision = 1;
  uint64_t _thermostatUntilMs = 0;
  bool _thermostatOverride = false, _thermostatCommand = false;
  Mode _thermostatMode = Mode::Temperature;
  int _thermostatValue = 0;
  std::atomic<TaskHandle_t> _task{nullptr};
  std::atomic<bool> _stop{false};
  std::atomic<bool> _startupDone{false};
  esp_err_t _startupResult = ESP_FAIL;
  esp_timer_handle_t _wakeTimer = nullptr;
  uint64_t _previousStep = 0, _workerGapUs = 0, _workerMaxGapUs = 0, _workerMaxStepUs = 0;
  struct FaultDetail {
    const char* kind = nullptr;
    unsigned side = 0;
    uint64_t atUs = 0, echoDeadlineUs = 0, workerGapUs = 0;
    uint64_t stepAgeUs = 0, rxReadAtUs = 0;
    int rxReadBytes = 0;
    int expectedByte = -1, receivedByte = -1, rxLevel = 0;
    size_t echoPosition = 0, echoPending = 0, bufferedRx = 0, queuedEvents = 0;
  } _faultDetail;
  bool _hardwareValidated = false, _active = false, _resetRequested = false;
  bool _paired = false;
  bool _bypassActive = false, _bypassRequested = false, _mirrorWaitingResponse = false;
  unsigned _qualifiedPairs = 0;
  uint64_t _mirrorResponseDeadline = 0, _switchReadyAt = 0;
  bool _waitingResponse = false, _sensorDirty = false, _sensorsValid = false;
  bool _hasSubmission = false;
  uint64_t _displayStamp = 0, _heaterStamp = 0, _responseDeadline = 0;
  uint64_t _observeDeadline = 0, _forwardedRequests = 0, _forwardedResponses = 0;
  uint64_t _framingErrors = 0, _echoErrors = 0, _timeouts = 0, _armAfter = 0;
  uint64_t _echoDelayedReads = 0, _echoMaxServiceDelayUs = 0;
  esp_err_t _lastError = ESP_OK;
  std::string _fault, _lastCommand = "none", _lastCommandResult = "none";
  p::Frame _stockFrame{}, _emittedFrame{}, _heaterFrame{};
  p::Request _request;
  p::Response _response;
  uint8_t _pendingCommand = 0, _observingCommand = 0;
  int _temperatureSetting = -1, _powerSetting = -1;
  std::string _configurationBootId;
  const char* _configurationPhase = "unconfigured";
  const char* _configurationEndedReason = nullptr;
  uint64_t _configurationRevision = 1;
  uint64_t _settingsRevision = 1, _submittedSettingsRevision = 0, _pairedSettingsRevision = 0;
  uint64_t _transportInterruption = 0;
  bool _submittedOperatingApplied = false, _pairedOperatingApplied = false;
  std::atomic<bool> _configWorkerRunning{false}, _configStop{false};
  struct ConfigJob {
    Impl* self;
    Cdhx* owner;
    std::unique_ptr<Response> response;
  };
  void runConfigJob(ConfigJob& job) {
    auto failResponse = [&](esp_err_t code, const char* message) {
      ErrorList errors;
      errors.check(code, message);
      auto* document = new JsonDocument();
      document->to<JsonArray>();
      errors.toJson(document->as<JsonVariant>());
      job.response->setError(document);
      job.response->publish();
    };
    if (_configStop.load()) {
      failResponse(ESP_ERR_INVALID_STATE, "Configuration worker stopped before accepting settings");
      return;
    }
    auto* data = job.response->requestData();
    ConfigJobRequest request(job.owner->interactiveName(),
        data ? data->as<JsonVariantConst>() : JsonVariantConst{});
    RequestContext context(request, request.data());
    const bool requireOperatingApplied = !request.data()["operating"].isUnbound();
    uint64_t revision = 0, responses = 0, interruption = 0;
    const bool changed = setServiceConfig(context, &revision, &responses, &interruption);
    if (!context.errors.empty()) {
      auto* document = new JsonDocument();
      document->to<JsonArray>();
      context.errors.toJson(document->as<JsonVariant>());
      job.response->setError(document);
      job.response->publish();
      return;
    }
    if (changed) {
      // Publication can synchronously call framework subscribers/getConfig.
      // Neither the UART owner nor the driver mutex may run this work.
      config::Changed::publish(job.owner);
      Broadcast::publish(job.owner->interactiveName(), "config-changed");
    }
    const auto deadline = nowUs() + 2000000;
    for (;;) {
      esp_err_t error = ESP_OK;
      const char* message = nullptr;
      std::unique_ptr<JsonDocument> result;
      {
        std::lock_guard lock(_mutex);
        if (_configStop.load()) {
          error = ESP_ERR_INVALID_STATE;
          message = "Settings retained; configuration worker stopped before forwarding confirmation";
        } else if (_settingsRevision != revision) {
          error = ESP_ERR_INVALID_STATE;
          message = "Settings were superseded; inspect current configuration before retrying";
        } else if (_transportInterruption != interruption ||
                   !_fault.empty() || !_active || _bypassRequested) {
          error = ESP_ERR_INVALID_STATE;
          message = "Settings retained; bridge unavailable before forwarding confirmation";
        } else {
          const bool complete = _forwardedResponses > responses && _paired && !_waitingResponse &&
              !_sides[0].echoPending && !_sides[1].echoPending &&
              _pairedSettingsRevision == revision &&
              (!requireOperatingApplied || _pairedOperatingApplied);
          if (complete) result.reset(serviceConfigLocked());
          else if (nowUs() >= deadline) {
            error = ESP_ERR_TIMEOUT;
            message = "Settings retained; forwarding confirmation timed out; inspect state before retrying";
          }
        }
      }
      if (error != ESP_OK) {
        failResponse(error, message);
        return;
      }
      if (result) {
        job.response->setData(result.release());
        job.response->publish();
        return;
      }
      vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(10)));
    }
  }
  Configuration _configuration;
  p::ServiceParameters _service;
  bool _serviceConfigured = false, _serviceSavePending = false;
  bool _operatingConfigured = false, _configurationActive = false, _configurationFlight = false;
  Sensor _voltage, _fan, _body, _ambient, _pump, _statusSensor;

  static uint8_t configurationMode(const Configuration& configuration) {
    return configuration.mode == Mode::Temperature ? p::ThermostatMode : p::FixedPowerMode;
  }
  bool fullyStopped() const {
    return _response.runState == 0 && _response.status == 0 &&
        _response.fanRpm == 0 && _response.pumpDecihz == 0;
  }
  bool powerQualified() const {
    return powerQualified(serviceBaseline());
  }
  bool powerQualified(const p::Frame& effective) const {
    return _options.fixedPowerMinimumRaw >= 0 && (!_options.requireFixedPowerTemplate ||
        (_displayStamp && p::sameServiceTemplate(effective, _options.fixedPowerTemplate)));
  }
  bool inputPowerValid(JsonVariantConst input, int power, const p::Frame& frame) const {
    return input["fixedPowerSettingRaw"].isUnbound() ||
        (powerQualified(frame) && power >= _options.fixedPowerMinimumRaw &&
         power <= _options.fixedPowerMaximumRaw);
  }
  p::Frame serviceBaseline() const {
    auto frame = _stockFrame;
    if (_serviceConfigured) p::patchService(frame, _service);
    return frame;
  }
  const char* serviceReason(uint64_t stamp) const {
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    if (research::engine().blocksLegacyControl()) return "Research owns heater control";
#endif
    if (!_serviceConfigured) return "Waiting for a valid normal display service tuple";
    if (!ready(stamp)) return "Active bridge and fresh normal frames required";
    if (!fullyStopped()) return "Service edits require a fully stopped heater";
    if (_request.command || _request.primeByte || _pendingCommand || _observingCommand ||
        _lastCommandResult == "uncertain-no-retry" ||
        _lastCommandResult == "outcome-unconfirmed-no-retry") return "Heater command unresolved";
    return nullptr;
  }
  const char* configurationReason(const Configuration& configuration, uint64_t stamp,
                                  bool admission) const {
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    if (research::engine().blocksLegacyControl()) return "Research owns heater control";
#endif
    if (!ready(stamp)) return "Active bridge and fresh normal frames required";
    if (_request.command || _pendingCommand || _observingCommand ||
        _lastCommandResult == "uncertain-no-retry" ||
        _lastCommandResult == "outcome-unconfirmed-no-retry") return "Heater command unresolved";
    (void)admission; // An edit affects the next request, never an in-flight frame.
    const bool changesMode = _operatingConfigured ? configuration.mode != _configuration.mode :
        configurationMode(configuration) != _request.mode;
    if (changesMode && !fullyStopped()) return "Mode changes require a fully stopped heater";
    if (!fullyStopped() && (_response.runState != 5 || _response.status != 1))
      return "Configuration requires standby or healthy steady running";
    if (configuration.mode == Mode::Temperature &&
        (!_request.thermostat() || _request.ambientTemperatureC < 0))
      return "Fresh stock thermostat ambient required";
    return nullptr;
  }
  void storeSetting(Mode mode, int value) {
    const bool changed = !_operatingConfigured || _configuration.mode != mode ||
        _configuration.value != value;
    _configuration.mode = mode;
    _configuration.value = value;
    if (mode == Mode::Temperature) _temperatureSetting = value;
    else _powerSetting = value;
    _operatingConfigured = true;
    _configurationActive = _configurationFlight = false;
    _configurationPhase = "pending";
    _configurationEndedReason = nullptr;
    if (changed) { ++_configurationRevision; ++_settingsRevision; _serviceSavePending = true; }
  }
  esp_err_t acceptSetting(Mode mode, int value) {
    if (mode != Mode::Temperature && mode != Mode::FixedPower) return ESP_ERR_INVALID_ARG;
    Configuration candidate;
    candidate.mode = mode;
    candidate.value = value;
    if (configurationReason(candidate, nowUs(), true)) return ESP_ERR_INVALID_STATE;
    auto patched = _stockFrame;
    if (!p::patchOperating(patched, configurationMode(candidate), value, _request.ambientTemperatureC))
      return ESP_ERR_INVALID_ARG;
    if (mode == Mode::FixedPower && (!powerQualified(serviceBaseline()) || value < _options.fixedPowerMinimumRaw ||
        value > _options.fixedPowerMaximumRaw)) return ESP_ERR_INVALID_ARG;
    storeSetting(mode, value);
    return ESP_OK;
  }
  p::Frame ownedBaseline() const {
    auto frame = serviceBaseline();
    if (_operatingConfigured)
      p::patchOperating(frame, configurationMode(_configuration), _configuration.value, _request.ambientTemperatureC);
    return frame;
  }
  void prepareConfiguration(p::Frame& outgoing, bool researchApplied) {
    _configurationActive = false;
    if (_thermostatOverride && nowUs() / 1000 >= _thermostatUntilMs)
      _thermostatOverride = false;
    if (_thermostatOverride && !researchApplied) {
      const bool compatible = (_thermostatMode == Mode::Temperature && _request.thermostat()) ||
          (_thermostatMode == Mode::FixedPower &&
           (_operatingConfigured ? _configuration.mode == Mode::FixedPower : _request.fixedPower()) &&
           powerQualified());
      if (compatible && p::patchOperating(outgoing,
              _thermostatMode == Mode::Temperature ? p::ThermostatMode : p::FixedPowerMode,
              _thermostatValue, _request.ambientTemperatureC)) return;
      manualTakeover();
    }
    if (!_operatingConfigured || researchApplied) return;
    if (_configuration.mode == Mode::FixedPower &&
        (!powerQualified(serviceBaseline()) ||
         _configuration.value < _options.fixedPowerMinimumRaw ||
         _configuration.value > _options.fixedPowerMaximumRaw)) {
      _configurationPhase = "suspended";
      _configurationEndedReason = "Saved power setting retained; forwarding display control until its range and service profile are qualified";
      return;
    }
    if (!p::patchOperating(outgoing, configurationMode(_configuration), _configuration.value,
                           _request.ambientTemperatureC)) {
      _configurationPhase = "suspended";
      _configurationEndedReason = "Saved settings retained; forwarding display control until fresh compatible ambient/limits return";
      return;
    }
    _configurationActive = true;
    _configurationEndedReason = nullptr;
  }

  bool hasBypass() const { return _options.bypassPin != GPIO_NUM_NC; }
  void selectBypass() {
    if (!hasBypass()) return;
    gpio_set_level(_options.bypassPin, !_options.interceptLevel);
    gpio_set_direction(_options.bypassPin, GPIO_MODE_OUTPUT);
    _bypassActive = true;
    _switchReadyAt = 0;
  }
  void clearMirrors() {
    _qualifiedPairs = 0;
    _mirrorWaitingResponse = false;
    for (auto& side : _sides) side.mirrorHead = side.mirrorCount = 0;
  }

  bool validPowerRange() const {
    const int lo = _options.fixedPowerMinimumRaw, hi = _options.fixedPowerMaximumRaw;
    return (lo == -1 && hi == -1) || (lo >= 0 && lo <= hi && hi <= 255);
  }
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
  research::Context researchContext() const {
    research::Context context;
    context.nowUs = nowUs();
    context.active = _active;
    context.fault = !_fault.empty();
    context.bypass = hasBypass() && _bypassActive;
    context.hasStock = _displayStamp != 0;
    context.hasHeater = _heaterStamp != 0;
    // Error counters survive reset, so cumulative request/reply totals cannot
    // establish correlation after an interrupted transaction.
    context.paired = _researchPaired && !_waitingResponse;
    context.stockFrame = _stockFrame;
    context.heaterFrame = _heaterFrame;
    context.stockStampUs = _displayStamp;
    context.heaterStampUs = _heaterStamp;
    context.freshnessMs = _options.freshnessMs;
    context.pendingCommand = _pendingCommand || _observingCommand;
    context.unresolvedCommand = _lastCommandResult == "uncertain-no-retry" ||
        _lastCommandResult == "outcome-unconfirmed-no-retry";
    context.legacyOverride = ownedBaseline() != _stockFrame;
    context.counters.crcErrors = _sides[0].parser.stats().crcErrors + _sides[1].parser.stats().crcErrors;
    context.counters.framingErrors = _framingErrors;
    context.counters.echoErrors = _echoErrors;
    context.counters.timeouts = _timeouts;
    return context;
  }
#endif
  bool fresh(uint64_t then, uint64_t stamp) const {
    return then && stamp >= then && stamp - then <= uint64_t(_options.freshnessMs) * 1000;
  }
  bool ready(uint64_t stamp) const {
    return _active && _fault.empty() && fresh(_displayStamp, stamp) &&
        fresh(_heaterStamp, stamp) && _request.primeByte == 0 &&
        (_request.thermostat() || _request.fixedPower()) &&
        (_request.command == 0 || _request.command == p::StartCommand || _request.command == p::StopCommand);
  }
  esp_err_t startupFailure(esp_err_t error) {
    if (_hardwareValidated) { releaseOutputs(); selectBypass(); }
    _lastError = error;
    _fault = "UART/GPIO initialization failed";
    return error;
  }
  void releaseOutputs() {
    for (auto& side : _sides) {
      gpio_reset_pin(side.hw.txPin); // Detach UART matrix; queued FIFO cannot escape.
      gpio_set_direction(side.hw.txPin, GPIO_MODE_INPUT);
      gpio_pullup_en(side.hw.txPin);
    }
  }
  void fail(const char* reason, esp_err_t error = ESP_ERR_INVALID_RESPONSE) {
    if (!_fault.empty()) return; // Preserve the initiating fault until explicit reset.
    ++_transportInterruption;
    manualTakeover();
    releaseOutputs(); // Release before formatting, flushing, or resetting state.
    selectBypass();
    _active = _waitingResponse = false;
    _paired = false;
    _configurationActive = _configurationFlight = false;
    _configurationPhase = "uncertain";
    _configurationEndedReason = reason;
    ++_configurationRevision;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    _researchPaired = false;
#endif
    _fault = reason;
    _lastError = error;
    if (_pendingCommand || _observingCommand)
      _lastCommandResult = "uncertain-no-retry";
    _pendingCommand = _observingCommand = 0;
    _sensorDirty = true;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    const auto context = researchContext();
    research::engine().fault(reason, context.nowUs, context.counters);
    research::engine().observe(context);
#endif
  }
  void recordFault(unsigned index, const char* kind, int receivedByte = -1) {
    if (_faultDetail.kind || !_fault.empty()) return;
    releaseOutputs();
    selectBypass(); // Stop FIFO tails before querying UART/queue state.
    const auto& side = _sides[index];
    _faultDetail.kind = kind;
    _faultDetail.side = index;
    _faultDetail.atUs = nowUs();
    _faultDetail.expectedByte = side.echoPending ? side.echo[side.echoPosition] : -1;
    _faultDetail.receivedByte = receivedByte;
    _faultDetail.echoPosition = side.echoPosition;
    _faultDetail.echoPending = side.echoPending;
    _faultDetail.echoDeadlineUs = side.echoDeadline;
    _faultDetail.workerGapUs = _workerGapUs;
    _faultDetail.stepAgeUs = _previousStep ? _faultDetail.atUs - _previousStep : 0;
    _faultDetail.rxReadAtUs = side.readObservedAt;
    _faultDetail.rxReadBytes = side.readCount;
    _faultDetail.rxLevel = gpio_get_level(side.hw.rxPin);
    uart_get_buffered_data_len(side.hw.uart, &_faultDetail.bufferedRx);
    _faultDetail.queuedEvents = uxQueueMessagesWaiting(side.events);
  }
  bool transactionTimeValid() {
    const auto stamp = nowUs();
    if (_active && _previousStep && stamp - _previousStep >
        uint64_t(std::min(_options.responseTimeoutMs, uint32_t(200))) * 1000) {
      ++_timeouts;
      recordFault(_sides[0].echoPending ? 0 : 1, "worker-stall");
      fail("UART owner stalled; transaction timing uncertain", ESP_ERR_TIMEOUT);
      return false;
    }
    if (_waitingResponse && stamp > _responseDeadline) {
      ++_timeouts;
      fail("Heater response timeout; no command retry", ESP_ERR_TIMEOUT);
      return false;
    }
    return true;
  }
  bool send(unsigned destination, const p::Frame& frame) {
    // An already queued receive error must prevent the next transmission,
    // even when it belongs to the endpoint visited later in the owner loop.
    for (auto& endpoint : _sides) checkEvents(endpoint);
    if (!_fault.empty()) return false;
    auto& side = _sides[destination];
    size_t buffered = 0;
    // Complete frames fit the S3 FIFO. Never block the owner for wire time.
    if (_sides[0].echoPending || _sides[1].echoPending ||
        uart_wait_tx_done(side.hw.uart, 0) != ESP_OK || !gpio_get_level(side.hw.rxPin) ||
        side.parser.buffered() || uart_get_buffered_data_len(side.hw.uart, &buffered) != ESP_OK || buffered) {
      fail("TX unavailable or DATA busy");
      return false;
    }
    if (!transactionTimeValid()) return false;
    p::Frame transmitted = frame;
    bool researchApplied = false;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    if (destination == 1) {
      researchApplied = research::engine().prepareTransmit(transmitted, researchContext());
      if (!transactionTimeValid()) return false;
    }
#endif
    if (destination == 1) prepareConfiguration(transmitted, researchApplied);
    // Persistent service authority is independent of temporary research leases, commands,
    // physical service edits, research release and bridge reset. Apply last so
    // any fallback to stock still carries the owned service tuple.
    if (destination == 1 && _serviceConfigured && !p::patchService(transmitted, _service)) {
      fail("Invalid service authority frame");
      return false;
    }
    if (!transactionTimeValid()) return false;
    side.echo = transmitted;
    side.echoPosition = 0;
    side.echoPending = transmitted.size();
    if (destination == 1) {
      _emittedFrame = transmitted;
      _hasSubmission = true;
      p::Request submitted;
      const bool decoded = p::decodeRequest(transmitted, submitted);
      const bool serviceMatches = !_serviceConfigured ||
          (decoded && p::serviceParameters(submitted) == _service);
      const bool operatingMatches = !_operatingConfigured ||
          (_configurationActive && decoded && submitted.mode == configurationMode(_configuration) &&
           submitted.requestedValueRaw == _configuration.value);
      // A service-only save can complete while saved thermostat control is
      // suspended for missing ambient. Operating saves additionally require
      // their selected mode/value to have actually reached this transaction.
      _submittedSettingsRevision = serviceMatches ? _settingsRevision : 0;
      _submittedOperatingApplied = operatingMatches;
    }
    // Reserve the wire-time budget at FIFO submission, after any bounded hook
    // work or cache pause. Trace preparation must not consume the echo window.
    const auto attemptedAt = nowUs();
    side.echoDeadline = attemptedAt + transmitted.size() * p::CharacterTimeUs + 10000;
    const int sent = uart_tx_chars(side.hw.uart,
        reinterpret_cast<const char*>(transmitted.data()), transmitted.size());
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    // Publish the saved pre-FIFO service timestamp after submission. No trace
    // mutex/cache pause may consume a validated action lease before this TX.
    research::engine().txAttempt(destination, transmitted, attemptedAt);
    research::engine().txResult(destination, sent, nowUs());
#else
    (void)attemptedAt;
#endif
    if (sent != static_cast<int>(transmitted.size())) {
      fail("Partial/failed TX; outcome uncertain; no retry", ESP_FAIL);
      return false;
    }
    return true;
  }
  void cancelRemote() {
    if (_observingCommand) _lastCommandResult = "outcome-unconfirmed-no-retry";
    else if (_pendingCommand) _lastCommandResult = "cancelled-by-display";
    _pendingCommand = _observingCommand = 0;
  }
  void manualTakeover(bool cancel = true) {
    ++_manualRevision;
    _thermostatOverride = false;
    if (cancel && _thermostatCommand) cancelRemote();
    _thermostatCommand = false;
  }
  void onDisplay(const p::Frame& stock) {
    if (!transactionTimeValid()) return;
    p::Request request;
    if (!p::decodeRequest(stock, request)) {
      fail("Unsupported display frame/header");
      return;
    }
    bool localChange = request.command != 0 || request.primeByte != 0;
    if (_displayStamp)
      for (size_t i = 0; i < 22; ++i)
        if (i != 2 && i != 3 && stock[i] != _stockFrame[i]) localChange = true;
    if (localChange) {
      manualTakeover();
      cancelRemote();
    }
    _stockFrame = stock;
    _request = request;
    _displayStamp = nowUs();
    if (!_serviceConfigured && stock[0] == 0x76 && request.command == 0 &&
        request.primeByte == 0 && (request.thermostat() || request.fixedPower()) &&
        p::decodeService(stock, _service)) {
      _serviceConfigured = true;
      _serviceSavePending = true;
      ++_configurationRevision;
      ++_settingsRevision;
    }
    if (!_operatingConfigured && stock[0] == 0x76 && request.command == 0 && request.primeByte == 0 &&
        (request.thermostat() || request.fixedPower())) {
      auto checked = stock;
      if (p::patchOperating(checked, request.mode, request.requestedValueRaw, request.ambientTemperatureC))
        storeSetting(request.fixedPower() ? Mode::FixedPower : Mode::Temperature, request.requestedValueRaw);
    }
    _sensorDirty = true;
    if (!_active) return;
    if (_waitingResponse) { fail("Overlapping display requests"); return; }
    p::Frame outgoing = stock;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    research::engine().beforeRequest(stock, researchContext(), outgoing);
#endif
    const auto pulse = _pendingCommand;
    if (pulse) {
      // Revalidate state at transmission time; never mask a physical command.
      const bool allowed = ready(nowUs()) && !localChange &&
          (!_thermostatCommand || nowUs() / 1000 < _thermostatUntilMs) &&
          (pulse == p::StartCommand ? (_response.runState == 0 && _response.status <= 1)
                                   : (_response.runState >= 1 && _response.runState <= 5));
      _pendingCommand = 0;
      if (allowed) p::patchCommand(outgoing, pulse);
      else _lastCommandResult = "cancelled-before-transmission";
    }
    // Set observation before attempting TX: even partial FIFO acceptance is
    // uncertain and must never leave a command eligible for replay.
    if (outgoing[2] == pulse && pulse) {
      _observingCommand = pulse;
      _observeDeadline = nowUs() + 5000000;
      _lastCommandResult = "sent-awaiting-observation";
    }
    if (!send(1, outgoing)) return;
    _configurationFlight = _configurationActive;
    if (_configurationFlight) _configurationPhase = "attempted";
    ++_forwardedRequests;
    _waitingResponse = true;
    _paired = false;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    _researchPaired = false;
#endif
    _responseDeadline = nowUs() + uint64_t(_options.responseTimeoutMs) * 1000;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    research::engine().observe(researchContext());
#endif
  }
  void onHeater(const p::Frame& frame) {
    if (!transactionTimeValid()) return;
    p::Response response;
    if (!p::decodeResponse(frame, response)) { fail("Unsupported heater response"); return; }
    if (!_active) return; // No transaction correlation while outputs are released.
    if (!_waitingResponse) { fail("Unsolicited heater response"); return; }
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    research::engine().pairedReply(frame, nowUs());
#endif
    // Retain the transaction deadline through TX preflight. A pause there must
    // neither forward an expired reply nor publish it as fresh heater state.
    if (!send(0, frame)) return;
    _waitingResponse = false;
    _paired = true;
    _pairedSettingsRevision = _submittedSettingsRevision;
    _pairedOperatingApplied = _submittedOperatingApplied;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    _researchPaired = true;
#endif
    ++_forwardedResponses;
    _response = response;
    _heaterFrame = frame;
    _heaterStamp = nowUs();
    if (_configurationFlight && _configurationActive) _configurationPhase = "paired-response";
    _configurationFlight = false;
    _sensorDirty = true;
    if (_observingCommand == p::StartCommand && response.runState >= 1 && response.runState <= 5) {
      _lastCommandResult = "observed-start-sequence";
      _observingCommand = 0;
    } else if (_observingCommand == p::StopCommand && (response.runState == 0 ||
               (response.runState >= 6 && response.runState <= 8))) {
      _lastCommandResult = "observed-shutdown-sequence";
      _observingCommand = 0;
    }
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    research::engine().observe(researchContext());
#endif
  }
  void checkEvents(Side& side) {
    if (!_fault.empty()) return;
    uart_event_t event{};
    while (xQueueReceive(side.events, &event, 0) == pdTRUE) {
      if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
          event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR || event.type == UART_BREAK) {
        ++_framingErrors;
        const char* kind = event.type == UART_FIFO_OVF ? "uart-fifo-overflow" :
            event.type == UART_BUFFER_FULL ? "uart-buffer-full" :
            event.type == UART_FRAME_ERR ? "uart-frame-error" :
            event.type == UART_PARITY_ERR ? "uart-parity-error" : "uart-break";
        recordFault(&side == &_sides[0] ? 0 : 1, kind);
        fail(kind);
        uart_flush_input(side.hw.uart);
        xQueueReset(side.events);
        side.parser.reset();
        return;
      }
    }
  }
  void readSide(unsigned index) {
    auto& side = _sides[index];
    checkEvents(side);
    if (!_fault.empty()) return;
    uint8_t bytes[128];
    // Keep the empty-read observation with this UART. A later endpoint or
    // callback may be interrupted by a cache/Flash pause while new RX arrives.
    side.readObservedAt = nowUs();
    const int count = side.readCount = uart_read_bytes(side.hw.uart, bytes, sizeof(bytes), 0);
    if (count < 0) {
      ++_framingErrors;
      recordFault(index, "uart-read-error");
      fail("UART receive read failed", ESP_FAIL);
      return;
    }
    if (count == 0) return;
    side.rxBytes += count;
    side.lastRx = nowUs();
    bool delayedEcho = false;
    for (int i = 0; i < count; ++i) {
      if (side.echoPending) {
        if (bytes[i] != side.echo[side.echoPosition]) {
          ++_echoErrors;
          recordFault(index, "echo-mismatch", bytes[i]);
          fail("Local TX echo byte mismatch");
          return;
        }
        // UART RX has no byte arrival timestamps. A correct queued byte read
        // after a flash/cache or scheduling pause is not evidence of late wire
        // delivery. Consume only the exact pending echo, never forward it, and
        // check the deadline for any still-missing bytes after both UART drains.
        const auto serviced = nowUs();
        if (serviced > side.echoDeadline) {
          delayedEcho = true;
          _echoMaxServiceDelayUs = std::max(_echoMaxServiceDelayUs, serviced - side.echoDeadline);
        }
        ++side.echoPosition;
        --side.echoPending;
        if (!side.echoPending && index == 1 && _configurationFlight && _configurationActive)
          _configurationPhase = "local-echo";
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
        if (!side.echoPending) research::engine().echoComplete(index, nowUs());
#endif
        continue;
      }
      const auto before = side.parser.stats();
      p::Frame completed;
      const bool complete = side.parser.feed(bytes[i], nowUs(), completed, false);
      const auto after = side.parser.stats();
      if ((_active || (hasBypass() && _bypassActive)) &&
          (before.crcErrors != after.crcErrors || before.lengthErrors != after.lengthErrors ||
          before.truncated != after.truncated || before.noiseBytes != after.noiseBytes)) {
        fail("Invalid/noisy/truncated DATA packet");
        return;
      }
      if (complete && _fault.empty()) {
        if (hasBypass() && _bypassActive) {
          if (side.mirrorCount == side.mirrors.size()) {
            fail("Bypass observation queue overflow");
            return;
          }
          side.mirrors[(side.mirrorHead + side.mirrorCount) % side.mirrors.size()] = completed;
          ++side.mirrorCount;
        } else if (index == 0) onDisplay(completed); else onHeater(completed);
        if (!_fault.empty()) return;
      }
    }
    if (delayedEcho) ++_echoDelayedReads;
  }
  void qualifyMirrors() {
    auto& display = _sides[0];
    auto& heater = _sides[1];
    while (display.mirrorCount && heater.mirrorCount) {
      const auto frame = display.mirrors[display.mirrorHead];
      const bool same = frame == heater.mirrors[heater.mirrorHead];
      display.mirrorHead = (display.mirrorHead + 1) % display.mirrors.size();
      heater.mirrorHead = (heater.mirrorHead + 1) % heater.mirrors.size();
      --display.mirrorCount;
      --heater.mirrorCount;
      if (!same) { fail("Bypass RX taps disagree; interception refused"); return; }
      // In bypass both taps see both directions. Establish phase using the
      // tested normal response shape as well as request fields, never just 76.
      // Unknown/ambiguous variants remain in stock bypass rather than guessing.
      const bool responseShape = p::validResponse(frame) && frame[2] <= 8 &&
          frame[3] <= 11 && frame[20] == 100 && frame[21] == 0;
      if (!_mirrorWaitingResponse) {
        p::Request request;
        // Acquire phase from known normal request fields; responses and
        // requests share 76. Never infer direction solely from the header.
        if (responseShape || !p::decodeRequest(frame, request) ||
            (!request.thermostat() && !request.fixedPower()) || request.primeByte != 0 ||
            (request.command != 0 && request.command != p::StartCommand && request.command != p::StopCommand)) {
          _qualifiedPairs = 0;
          continue;
        }
        _stockFrame = frame;
        _request = request;
        _displayStamp = nowUs();
        _mirrorWaitingResponse = true;
        _mirrorResponseDeadline = nowUs() + uint64_t(_options.responseTimeoutMs) * 1000;
      } else {
        p::Response response;
        if (!responseShape || !p::decodeResponse(frame, response) || nowUs() > _mirrorResponseDeadline) {
          clearMirrors();
          return;
        }
        _mirrorWaitingResponse = false;
        _response = response;
        _heaterFrame = frame;
        _heaterStamp = nowUs();
        _qualifiedPairs = std::min(3u, _qualifiedPairs + 1);
      }
    }
  }
  void step() {
    auto stamp = nowUs();
    _workerGapUs = _previousStep && stamp >= _previousStep ? stamp - _previousStep : 0;
    _workerMaxGapUs = std::max(_workerMaxGapUs, _workerGapUs);
    _previousStep = stamp;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
    research::engine().tick(researchContext());
#endif
    if (_resetRequested) {
      releaseOutputs();
      selectBypass();
      cancelRemote();
      _waitingResponse = _resetRequested = false;
      _paired = false;
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
      _researchPaired = false;
#endif
      _displayStamp = _heaterStamp = 0;
      _fault.clear();
      _faultDetail = {};
      _lastError = ESP_OK;
      clearMirrors();
      for (auto& side : _sides) {
        uart_flush_input(side.hw.uart);
        xQueueReset(side.events);
        side.parser.reset();
        side.echoPending = 0;
        side.lastRx = stamp;
      }
      _armAfter = stamp + uint64_t(_options.quietBeforeArmMs) * 1000;
    }
    if (!_fault.empty()) {
      // The display continues polling after TX release. Discard RX while
      // latched so a secondary ring-buffer overrun cannot hide the first fault.
      for (auto& side : _sides) {
        uart_flush_input(side.hw.uart);
        xQueueReset(side.events);
      }
      return;
    }
    // Do not treat an unbounded backlog as a fresh transaction. A bounded
    // owner pause may drain exact echoes, but cannot revive an expired reply or
    // preserve transaction correlation across a long servicing outage.
    if (_active && _workerGapUs > uint64_t(std::min(_options.responseTimeoutMs, uint32_t(200))) * 1000) {
      ++_timeouts;
      recordFault(_sides[0].echoPending ? 0 : 1, "worker-stall");
      fail("UART owner stalled; transaction timing uncertain", ESP_ERR_TIMEOUT);
      return;
    }
    if (_waitingResponse && stamp > _responseDeadline) {
      ++_timeouts;
      fail("Heater response timeout; no command retry", ESP_ERR_TIMEOUT);
      return;
    }
    for (auto& side : _sides) checkEvents(side);
    if (!_fault.empty()) return;
    for (unsigned i = 0; i < _sides.size(); ++i) readSide(i);
    if (!_fault.empty()) return;
    if (!transactionTimeValid()) return;
    // RX timestamps were captured while draining the UARTs; evaluate deadlines
    // afterwards so a newly buffered partial frame cannot appear time-reversed.
    stamp = nowUs();
    if (hasBypass() && _bypassActive) {
      qualifyMirrors();
      if (!_fault.empty()) return;
      if (_mirrorWaitingResponse && stamp > _mirrorResponseDeadline) clearMirrors();
    }
    for (auto& side : _sides) {
      // A nonempty partial batch needs another normal service pass. Only an
      // empty read after the deadline establishes absence; use its own time,
      // never a later loop timestamp that may include a Flash/scheduling pause.
      if (side.readCount == 0 && side.echoPending && side.readObservedAt > side.echoDeadline) {
        ++_echoErrors;
        recordFault(&side == &_sides[0] ? 0 : 1, "echo-missing");
        fail("Local TX echo missing", ESP_ERR_TIMEOUT);
        return;
      }
      if (side.readCount == 0 && side.parser.expire(side.readObservedAt) &&
          (_active || (hasBypass() && _bypassActive))) {
        fail("Incomplete DATA frame", ESP_ERR_TIMEOUT);
        return;
      }
    }
    if (_waitingResponse && stamp > _responseDeadline) {
      ++_timeouts;
      fail("Heater response timeout; no command retry", ESP_ERR_TIMEOUT);
      return;
    }
    if (_active && _displayStamp && !fresh(_displayStamp, stamp)) {
      ++_timeouts;
      fail("Physical display link lost", ESP_ERR_TIMEOUT);
      return;
    }
    if (_observingCommand && stamp > _observeDeadline) {
      _lastCommandResult = "outcome-unconfirmed-no-retry";
      _observingCommand = 0;
    }
    if (!_active && stamp >= _armAfter) {
      // Configuration is restored during App initialization. Do not intercept
      // with transient display tuning before the persisted authority is loaded.
      if (!App::initialized()) return;
      if (_bypassRequested || (hasBypass() && _bypassActive &&
          (_qualifiedPairs < 3 || _mirrorWaitingResponse ||
           _sides[0].mirrorCount || _sides[1].mirrorCount || !fresh(_heaterStamp, stamp)))) return;
      for (auto& side : _sides)
        if (side.parser.buffered() || stamp < side.lastRx ||
            stamp - side.lastRx < uint64_t(_options.quietBeforeArmMs) * 1000 ||
            !gpio_get_level(side.hw.rxPin) || uart_wait_tx_done(side.hw.uart, 0) != ESP_OK) return;
      if (hasBypass() && _bypassActive) {
        // Last mirrored packet was a complete heater response. Open the stock
        // link with both sinks released, then wait for the switch to settle.
        gpio_set_level(_options.bypassPin, _options.interceptLevel);
        _bypassActive = false;
        _switchReadyAt = stamp + uint64_t(_options.bypassSettleMs) * 1000;
        return;
      }
      if (stamp < _switchReadyAt) return;
      for (auto& side : _sides) {
        gpio_set_level(side.hw.txPin, 1);
        if (uart_set_pin(side.hw.uart, side.hw.txPin, side.hw.rxPin,
                         UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
          fail("Unable to attach UART transmit pins");
          return;
        }
      }
      _active = true;
    }
  }
  void updateSensors() {
    if (!App::initialized()) return;
    p::Response response;
    p::Request request;
    bool valid = false, displayValid = false;
    uint64_t displaySampleMs = 0, heaterSampleMs = 0;
    {
      std::lock_guard lock(_mutex);
      valid = _active && fresh(_heaterStamp, nowUs());
      displayValid = _active && fresh(_displayStamp, nowUs());
      if (!_sensorDirty && valid == _sensorsValid) return;
      _sensorDirty = false;
      _sensorsValid = valid;
      response = _response;
      request = _request;
      displaySampleMs = _displayStamp / 1000;
      heaterSampleMs = _heaterStamp / 1000;
    }
    // Component events can call framework handlers; publish outside our mutex.
    if (valid) {
      _voltage.set(response.supplyVoltageDeciv / 10.0f);
      _fan.set(response.fanRpm);
      if (response.status != 10) _body.setSample(response.bodyTemperatureC, heaterSampleMs); else _body.resetState();
      _pump.set(response.pumpDecihz / 10.0f);
      _statusSensor.set(response.status);
    } else {
      _voltage.resetState(); _fan.resetState(); _body.resetState();
      _pump.resetState(); _statusSensor.resetState();
    }
    if (displayValid && request.thermostat()) _ambient.setSample(request.ambientTemperatureC, displaySampleMs);
    else _ambient.resetState();
  }
  void run() {
    while (!_task.load()) vTaskDelay(1);
    _startupResult = initializeHardware();
    _startupDone.store(true);
    if (_startupResult != ESP_OK) {
      _task.store(nullptr);
      vTaskDelete(nullptr);
      return;
    }
    _armAfter = nowUs() + uint64_t(_options.quietBeforeArmMs) * 1000;
    bool watchdog = false;
    while (!_stop.load()) {
      if (!watchdog && App::initialized()) watchdog = esp_task_wdt_add(nullptr) == ESP_OK;
      {
        std::lock_guard lock(_mutex);
        step();
        _workerMaxStepUs = std::max(_workerMaxStepUs, nowUs() - _previousStep);
      }
      if (watchdog) esp_task_wdt_reset();
      // High-resolution notifications give a 1ms service cadence even when a
      // consuming project retains the default 100Hz FreeRTOS tick. Coalesced
      // wakeups never replay traffic; strict echo/response deadlines still apply.
      ulTaskNotifyTake(pdTRUE, 1);
    }
    releaseOutputs();
    selectBypass();
    if (watchdog) esp_task_wdt_delete(nullptr);
    _task.store(nullptr);
    vTaskDelete(nullptr);
  }
  static void taskEntry(void* context) { static_cast<Impl*>(context)->run(); }
};

Cdhx::Cdhx(const char* name, const Options& options)
    : _name(name && *name ? name : "cdhx"), _impl(std::make_unique<Impl>(this, options)) {
  setSensorsPollInterval(250);
  Device::init(Flags::HasSensors);
  ESP_ERROR_CHECK_WITHOUT_ABORT(_impl->start());
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
  research::bindDriver(_impl.get(), [](void* context, const research::Action& action) {
    auto& impl = *static_cast<Impl*>(context);
    std::lock_guard lock(impl._mutex);
    const auto snapshot = impl.researchContext();
    research::engine().observe(snapshot);
    return research::engine().accept(action, snapshot.nowUs);
  });
#endif
}
Cdhx::~Cdhx() {
  _impl->stopConfigWorker();
#if defined(ESP32M_CDHX_RESEARCH) && ESP32M_CDHX_RESEARCH
  research::unbindDriver(_impl.get());
#endif
}
bool Cdhx::pollSensors() {
  _impl->updateSensors();
  // App persistence may call getConfig synchronously. Keep publication outside
  // the UART owner and driver mutex, including one-time initial adoption.
  if (_impl->takeServiceSavePending()) config::Changed::publish(this);
  return true;
}
esp_err_t Cdhx::turn(bool on) { return _impl->turn(on); }
thermostat::Capabilities Cdhx::capabilities() const { return _impl->thermostatCapabilities(); }
thermostat::Feedback Cdhx::snapshot() const { return _impl->thermostatSnapshot(); }
thermostat::Result Cdhx::apply(const thermostat::Request& request) { return _impl->thermostatApply(request); }
esp_err_t Cdhx::setTemperature(int value) {
  const auto error = _impl->setTemperature(value);
  if (error == ESP_OK && _impl->takeServiceSavePending()) config::Changed::publish(this);
  return error;
}
esp_err_t Cdhx::setFixedPower(int value) {
  const auto error = _impl->setFixedPower(value);
  if (error == ESP_OK && _impl->takeServiceSavePending()) config::Changed::publish(this);
  return error;
}
esp_err_t Cdhx::setConfiguration(const Configuration& configuration) {
  const auto error = _impl->setConfiguration(configuration);
  if (error == ESP_OK && _impl->takeServiceSavePending()) config::Changed::publish(this);
  return error;
}
esp_err_t Cdhx::releaseOverride() { return _impl->releaseOverride(); }
esp_err_t Cdhx::resetBridge() { return _impl->resetBridge(); }
esp_err_t Cdhx::setBypass(bool enabled) { return _impl->setBypass(enabled); }
const JsonVariantConst Cdhx::descriptor() const {
  static JsonDocument doc;
  if (doc.isNull()) doc.to<JsonArray>().add("cdhx");
  return doc.as<JsonArrayConst>();
}
bool Cdhx::handleRequest(Request& request) {
  if (!request.isInternal() && request.is("config-set")) {
    if (request.isBroadcast())
      request.respondError(ESP_ERR_INVALID_ARG, "Target the heater configuration explicitly");
    else
      _impl->startConfigWorker(this, request);
    return true;
  }
  return Device::handleRequest(request);
}
JsonDocument* Cdhx::getState(RequestContext&) {
  auto* doc = new JsonDocument();
  _impl->toState(doc->to<JsonObject>());
  return doc;
}
JsonDocument* Cdhx::getConfig(RequestContext&) { return _impl->serviceConfig(); }
bool Cdhx::setConfig(RequestContext& ctx) { return _impl->setServiceConfig(ctx); }
void Cdhx::setState(RequestContext& ctx) {
  if (!ctx.data.is<JsonObjectConst>() || ctx.data.size() != 1) {
    ctx.errors.add("Provide exactly one heater action");
    return;
  }
  if (!ctx.data["configuration"].isUnbound()) {
    if (ctx.request.isBroadcast() || !ctx.data["configuration"].is<JsonObjectConst>()) {
      ctx.errors.add("Configuration requires an explicit target and an object");
      return;
    }
    const auto object = ctx.data["configuration"].as<JsonObjectConst>();
    for (const auto member : object) {
      const char* key = member.key().c_str();
      if (std::strlen(key) != member.key().size() ||
          (std::strcmp(key, "expectedBootId") && std::strcmp(key, "expectedRevision") &&
           std::strcmp(key, "acceptBeforeUptimeMs") &&
           std::strcmp(key, "mode") && std::strcmp(key, "temperatureSetpointC") &&
           std::strcmp(key, "fixedPowerSettingRaw"))) {
        ctx.errors.add("Unknown or read-only configuration field");
        return;
      }
    }
    if (!object["mode"].is<const char*>()) {
      ctx.errors.add("Configuration mode is required");
      return;
    }
    Configuration configuration;
    const auto mode = object["mode"].as<std::string>();
    if (mode == "temperature" && object["temperatureSetpointC"].is<int>() &&
        object["fixedPowerSettingRaw"].isUnbound()) {
      configuration.mode = Mode::Temperature;
      configuration.value = object["temperatureSetpointC"].as<int>();
    } else if (mode == "power" && object["fixedPowerSettingRaw"].is<int>() &&
               object["temperatureSetpointC"].isUnbound()) {
      configuration.mode = Mode::FixedPower;
      configuration.value = object["fixedPowerSettingRaw"].as<int>();
    } else {
      ctx.errors.add("Provide the selected mode's integer setpoint only");
      return;
    }
    ctx.errors.check(setConfiguration(configuration));
  }
  else if (ctx.data["on"].is<bool>()) ctx.errors.check(turn(ctx.data["on"].as<bool>()));
  else if (ctx.data["bypass"].is<bool>()) ctx.errors.check(setBypass(ctx.data["bypass"].as<bool>()));
  else if (ctx.data["temperatureSetpointC"].is<int>())
    ctx.errors.check(setTemperature(ctx.data["temperatureSetpointC"].as<int>()));
  else if (ctx.data["fixedPowerSettingRaw"].is<int>())
    ctx.errors.check(setFixedPower(ctx.data["fixedPowerSettingRaw"].as<int>()));
  else if (ctx.data["releaseOverride"].is<bool>() && ctx.data["releaseOverride"].as<bool>())
    ctx.errors.check(releaseOverride());
  else if (ctx.data["resetBridge"].is<bool>() && ctx.data["resetBridge"].as<bool>())
    ctx.errors.check(resetBridge());
  else ctx.errors.add("Unsupported heater action or invalid value type");
}
Cdhx* useCdhx(const Cdhx::Options& options, const char* name) { return new Cdhx(name, options); }
}  // namespace esp32m::dev
