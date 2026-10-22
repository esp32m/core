#include "esp32m/dev/lf-bros.hpp"

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_twai.h>
#include <esp_twai_onchip.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#if CONFIG_IDF_TARGET_ESP32S3
#include <hal/twai_ll.h>
#include <soc/twai_struct.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

#include "esp32m/events.hpp"
#include "esp32m/integrations/ha/ha.hpp"

namespace {
  constexpr const char* Tag = "lf-bros";

  constexpr uint32_t ControlIdentifier = 0x201;
  constexpr uint32_t RuntimeRequestIdentifier = 0x205;
  constexpr uint32_t ControllerResetIdentifier = 0x207;
  constexpr uint32_t ResponseIdentifier = 0xAA55;
  constexpr uint32_t NominalBitrate = 150'000;
  constexpr uint32_t ActualBitrate = 80'000'000 / (28 * 19);
  constexpr size_t RuntimeLength = 180;
  constexpr size_t RuntimeChunks = (RuntimeLength + 7) / 8;
  constexpr uint32_t RuntimeChunkMask =
      (static_cast<uint32_t>(1) << RuntimeChunks) - 1;
  constexpr int64_t ColdBootDelayUs = 1'000'000;
  constexpr int64_t ClaimDelayUs = 250'000;
  constexpr int64_t HeaderMaxAgeUs = 100'000;
  constexpr int64_t ConnectionTimeoutUs = 5'000'000;
  constexpr int64_t NewSessionGapUs = 5'000'000;

  struct TwaiHardwareRxStatus {
    uint32_t status = 0;
    uint32_t pending = 0;
  };

  inline TwaiHardwareRxStatus readTwaiHardwareRxStatus() {
#if CONFIG_IDF_TARGET_ESP32S3
    return {twai_ll_get_status(&TWAI), twai_ll_get_rx_msg_count(&TWAI)};
#else
    return {};
#endif
  }

  inline void updateMaximum32(std::atomic<uint32_t>& maximum,
                              uint32_t value) {
    auto previous = maximum.load(std::memory_order_relaxed);
    while (value > previous &&
           !maximum.compare_exchange_weak(previous, value,
                                          std::memory_order_relaxed)) {
    }
  }

  constexpr uint8_t RuntimeMagic[8] = {'E', 'C', '3', '.',
                                       '8', '0', 'a', 0};

  enum class SessionStage : uint8_t {
    Starting,
    Prearmed,
    Probing,
    ApplicationSeen,
    Claiming,
    Active,
    Error,
  };

  const char* stageName(SessionStage stage) {
    switch (stage) {
      case SessionStage::Starting:
        return "starting";
      case SessionStage::Prearmed:
        return "prearmed";
      case SessionStage::Probing:
        return "probing";
      case SessionStage::ApplicationSeen:
        return "application-seen";
      case SessionStage::Claiming:
        return "claiming";
      case SessionStage::Active:
        return "active";
      case SessionStage::Error:
        return "error";
    }
    return "unknown";
  }

  const char* controlModeName(esp32m::dev::Lfbros::ControlMode mode) {
    return mode == esp32m::dev::Lfbros::ControlMode::Temperature
               ? "temperature"
               : "power";
  }

  bool parseControlMode(const char* value,
                        esp32m::dev::Lfbros::ControlMode& result) {
    if (!value)
      return false;
    if (!strcasecmp(value, "temperature")) {
      result = esp32m::dev::Lfbros::ControlMode::Temperature;
      return true;
    }
    if (!strcasecmp(value, "power")) {
      result = esp32m::dev::Lfbros::ControlMode::Power;
      return true;
    }
    return false;
  }

  const char* heaterStateName(uint8_t state) {
    switch (state) {
      case 0:
        return "initialization";
      case 1:
        return "standby-off";
      case 2:
        return "start-request-delay";
      case 3:
        return "thermostat-restart";
      case 4:
        return "glow-fan-ramp";
      case 5:
        return "preheat";
      case 6:
        return "pre-pump-or-retry-reset";
      case 7:
        return "fuel-admission";
      case 8:
        return "ignition-ramp-1";
      case 9:
        return "ignition-ramp-2-flame-check";
      case 10:
        return "ignition-stabilization";
      case 11:
        return "high-fire-warmup";
      case 12:
        return "regulated-control-entry";
      case 13:
        return "regulated-running";
      case 15:
        return "thermostat-burner-stop";
      case 16:
        return "thermostat-idle";
      case 17:
        return "shutdown-entry";
      case 18:
        return "controlled-shutdown";
      case 19:
        return "purge-cooldown";
      case 20:
        return "final-fan-rundown";
      case 21:
        return "mode1-stage-21";
      case 22:
        return "mode1-stage-22";
      case 23:
        return "mode1-pumping";
      case 24:
        return "mode1-stage-24";
      case 25:
        return "mode1-stage-25";
      case 26:
        return "mode1-stage-26";
      case 28:
        return "fan-only";
      case 29:
        return "startup-settle";
      case 30:
        return "fault-purge";
      case 31:
        return "fault-latched";
      default:
        return "unknown";
    }
  }

  const char* heaterStateGroup(uint8_t state) {
    if (state == 0)
      return "initializing";
    if (state == 1)
      return "standby-off";
    if (state >= 21 && state <= 26)
      return "special-mode-1";
    if (state == 15 || state == 16)
      return "thermostat-idle";
    if (state == 17 || state == 18 || state == 19 || state == 20 ||
        state == 30)
      return "shutdown-cooldown";
    if (state == 28)
      return "ventilation";
    if ((state >= 2 && state <= 17) || state == 29)
      return "active-sequence";
    if (state == 31)
      return "fault";
    return "special-transition";
  }

  const char* climateAction(uint8_t state, bool valid) {
    if (!valid || state == 0 || state == 1 || state == 31)
      return "off";
    if (state == 15 || state == 16)
      return "idle";
    if (state == 17 || state == 18 || state == 19 || state == 20 ||
        state == 28 || state == 30)
      return "fan";
    if (state == 13)
      return "heating";
    if (state >= 2 && state <= 29)
      return "heating";
    return "idle";
  }

  const char* operationModeName(uint8_t mode) {
    switch (mode) {
      case 0:
        return "normal-heating";
      case 1:
        return "special-polr-candidate";
      case 2:
        return "fan-only";
      default:
        return "unknown";
    }
  }

  const char* twaiStateName(int state) {
    switch (static_cast<twai_error_state_t>(state)) {
      case TWAI_ERROR_ACTIVE:
        return "error-active";
      case TWAI_ERROR_WARNING:
        return "error-warning";
      case TWAI_ERROR_PASSIVE:
        return "error-passive";
      case TWAI_ERROR_BUS_OFF:
        return "bus-off";
    }
    return "unknown";
  }

  uint32_t readU32(const uint8_t* data) {
    uint32_t value;
    std::memcpy(&value, data, sizeof(value));
    return value;
  }

  int32_t readI32(const uint8_t* data) {
    int32_t value;
    std::memcpy(&value, data, sizeof(value));
    return value;
  }

  float readFloat(const uint8_t* data) {
    float value;
    std::memcpy(&value, data, sizeof(value));
    return value;
  }

  uint32_t stm32Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t offset = 0; offset < size; offset += 4) {
      uint8_t wordBytes[4] = {};
      const auto count = std::min<size_t>(4, size - offset);
      std::memcpy(wordBytes, data + offset, count);
      crc ^= readU32(wordBytes);
      for (int bit = 0; bit < 32; bit++)
        crc = crc & 0x80000000U ? (crc << 1U) ^ 0x04C11DB7U
                                : crc << 1U;
    }
    return crc;
  }

  int displayErrorCode(const uint8_t* payload) {
    auto value = payload[0x4D];
    if (value & 0x01)
      return value & 0x10 && !(value & 0x20) ? 15 : 0;
    if (payload[0x4E] & 0x01)
      return 3;
    value = payload[0x48];
    if (value & 0x01)
      return value & 0x04 ? 12 : 9;
    if (payload[0x49] & 0x01)
      return 8;
    if (payload[0x4B] & 0x01)
      return 6;
    if (payload[0x4A] & 0x01)
      return 7;
    value = payload[0x4C];
    if (value & 0x01)
      return value & 0x04 ? 11 : 10;
    value = payload[0x4F];
    if (value & 0x01)
      return value & 0x04 ? 2 : 1;
    if (payload[0x50] & 0x01)
      return 5;
    return 0xFF;
  }

  class ClimateComponent : public esp32m::dev::Component {
   public:
    explicit ClimateComponent(esp32m::Device* device)
        : Component("climate") {
      _flags = static_cast<Flags>(Flags::RetainState | Flags::HasState |
                                  Flags::AcceptsCommands);
      Component::init(device);
      setTitle("Heater");
    }
    const char* component() const override {
      return "climate";
    }
    const char* type() const override {
      return "";
    }
  };

  class NumberComponent : public esp32m::dev::Component {
   public:
    NumberComponent(esp32m::Device* device, const char* id) : Component(id) {
      _flags = static_cast<Flags>(Flags::RetainState | Flags::HasState |
                                  Flags::AcceptsCommands);
      Component::init(device);
    }
    const char* component() const override {
      return "number";
    }
    const char* type() const override {
      return "";
    }
  };

  const esp32m::dev::Select::TOptions ControlModeOptions = {"temperature",
                                                            "power"};
}  // namespace

namespace esp32m {
  namespace dev {

    class Lfbros::Impl {
     public:
      struct Desired {
        ControlMode controlMode = ControlMode::Power;
        float temperatureC = 18.0f;
        float powerW = 2000.0f;
      };

      struct Runtime {
        bool valid = false;
        bool partial = false;
        uint32_t chunkMask = 0;
        char firmwareTag[9] = {};
        uint8_t state = 0;
        uint8_t nextState = 0;
        uint8_t thermalSubstate = 0;
        uint8_t fanControlState = 0;
        uint8_t fanControlNextState = 0;
        uint8_t faultFlags[10] = {};
        bool faultActive = false;
        int faultCode = 0xFF;

        uint8_t displayOperationMode = 0;
        uint8_t displayControlMode = 0;
        uint32_t displaySpecialTarget = 0;
        float displayTemperatureC = NAN;
        float displayPowerW = NAN;
        uint8_t displayOnOff = 0;

        float pumpFrequencyHz = NAN;
        uint32_t pumpPulseCount = 0;
        float glowResistanceOhm = NAN;
        float glowVoltageV = NAN;
        float glowMinimumVoltageV = NAN;
        float glowDuty = NAN;
        float glowPowerW = NAN;
        int32_t fanRpm = 0;
        float fanDrive = NAN;
        int32_t fanTargetRpm = 0;
        float sensorA_C = NAN;
        float sensorB_C = NAN;
        float chamberC = NAN;
        float ambientC = NAN;
        float supplyVoltageV = NAN;
        int64_t receivedUs = 0;
      };

      enum class CommandType : uint8_t {
        TurnOn,
        TurnOff,
        ClearFault,
        StartPriming,
        ApplySetpoint
      };
      struct Command {
        CommandType type;
        bool thermostat = false;
        uint64_t revision = 0;
        uint64_t validUntilMs = 0;
        uint64_t lease = 0;
      };

      struct RxFrame {
        uint32_t id = 0;
        uint64_t timestampUs = 0;
        uint8_t dlc = 0;
        bool extended = false;
        bool remote = false;
        uint8_t data[8] = {};
      };

      Impl(Lfbros* owner, const Options& options)
          : _owner(owner),
            _options(options),
            _climate(owner),
            _powerSetpoint(owner, "power_setpoint"),
            _controlMode(owner, ControlModeOptions, "control_mode"),
            _connectedSensor(owner, "connectivity", "connected"),
            _faultSensor(owner, "problem", "fault"),
            _stateSensor(owner, "", "heater_state"),
            _chamberSensor(owner, "temperature", "chamber_temperature"),
            _ambientSensor(owner, "temperature", "ambient_temperature"),
            _voltageSensor(owner, "voltage", "supply_voltage"),
            _fanSensor(owner, "", "fan_speed"),
            _pumpSensor(owner, "frequency", "pump_frequency"),
            _glowPowerSensor(owner, "power", "glow_power"),
            _faultCodeSensor(owner, "", "fault_code") {
        _desired.controlMode = options.initialControlMode;
        _desired.temperatureC = std::clamp(options.initialTemperatureC, 10.0f,
                                           35.0f);
        // start() rejects an invalid power range. Avoid calling std::clamp
        // with reversed bounds before that validation has run.
        _desired.powerW = options.maximumPowerW > options.minimumPowerW
                              ? std::clamp(options.initialPowerW,
                                           options.minimumPowerW,
                                           options.maximumPowerW)
                              : options.initialPowerW;

        _powerSetpoint.setTitle("Power setpoint");
        _controlMode.setTitle("Control mode");
        _connectedSensor.setTitle("Heater connection");
        _faultSensor.setTitle("Heater fault");
        _stateSensor.setTitle("Heater state");
        _chamberSensor.setTitle("Chamber temperature");
        _ambientSensor.setTitle("Ambient temperature");
        _voltageSensor.setTitle("Heater supply voltage");
        _fanSensor.setTitle("Fan speed");
        _fanSensor.unit = "rpm";
        _pumpSensor.setTitle("Pump frequency");
        _glowPowerSensor.setTitle("Glow-plug power");
        _faultCodeSensor.setTitle("Display fault code");

        _chamberSensor.precision = 1;
        _ambientSensor.precision = 1;
        _voltageSensor.precision = 2;
        _pumpSensor.precision = 2;
        _glowPowerSensor.precision = 1;
        _powerSetpoint.precision = 0;
        _chamberSensor.stateClass = StateClass::Measurement;
        _ambientSensor.stateClass = StateClass::Measurement;
        _voltageSensor.stateClass = StateClass::Measurement;
        _fanSensor.stateClass = StateClass::Measurement;
        _pumpSensor.stateClass = StateClass::Measurement;
        _glowPowerSensor.stateClass = StateClass::Measurement;
      }

      ~Impl() {
        _running.store(false);
        if (_task) {
          vTaskDelay(1);
          vTaskDelete(_task);
          _task = nullptr;
        }
        if (_node) {
          twai_node_disable(_node);
          twai_node_delete(_node);
          _node = nullptr;
        }
        if (_rxQueue)
          vQueueDelete(_rxQueue);
        if (_commandQueue)
          vQueueDelete(_commandQueue);
        if (_txDone)
          vSemaphoreDelete(_txDone);
      }

      esp_err_t start() {
        _stage.store(SessionStage::Starting);
        if (!GPIO_IS_VALID_OUTPUT_GPIO(_options.hardware.txPin) ||
            !GPIO_IS_VALID_GPIO(_options.hardware.rxPin)) {
          setError(ESP_ERR_INVALID_ARG);
          return ESP_ERR_INVALID_ARG;
        }
        if (!std::isfinite(_options.minimumPowerW) ||
            !std::isfinite(_options.maximumPowerW) ||
            _options.minimumPowerW <= 0 ||
            _options.maximumPowerW <= _options.minimumPowerW ||
            _options.pollIntervalMs < 250) {
          setError(ESP_ERR_INVALID_ARG);
          return ESP_ERR_INVALID_ARG;
        }

        _rxQueue = xQueueCreate(64, sizeof(RxFrame));
        _commandQueue = xQueueCreate(12, sizeof(Command));
        _txDone = xSemaphoreCreateBinary();
        if (!_rxQueue || !_commandQueue || !_txDone) {
          setError(ESP_ERR_NO_MEM);
          return ESP_ERR_NO_MEM;
        }

        twai_onchip_node_config_t config = {};
        config.io_cfg.tx = _options.hardware.txPin;
        config.io_cfg.rx = _options.hardware.rxPin;
        config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
        config.io_cfg.bus_off_indicator = GPIO_NUM_NC;
        // A valid placeholder is required before the exact advanced timing is
        // applied. The basic calculator cannot represent 150 kbit/s on S3.
        config.bit_timing.bitrate = 125'000;
        config.timestamp_resolution_hz = 1'000'000;
        // A pump or fan switching edge can corrupt an individual request.
        // Bounded hardware retries improve delivery without allowing a broken
        // bus to hold the worker indefinitely.
        config.fail_retry_cnt = 3;
        config.tx_queue_depth = 2;

        auto result = twai_new_node_onchip(&config, &_node);
        if (result != ESP_OK) {
          setError(result);
          return result;
        }

        twai_timing_advanced_config_t timing = {};
        timing.brp = 28;
        timing.prop_seg = 3;
        // Keep the measured 150.376 kbit/s rate (19 Tq), but sample earlier
        // and allow more resynchronization margin.  On the SJA1000-compatible
        // ESP32-S3 TWAI block any non-zero ssp_offset enables triple sampling;
        // the numeric offset itself is otherwise ignored by the v1 HAL.
        timing.tseg_1 = 11;
        timing.tseg_2 = 4;
        timing.sjw = 3;
        timing.ssp_offset = 1;
        result = twai_node_reconfig_timing(_node, &timing, nullptr);
        if (result != ESP_OK) {
          setError(result);
          return result;
        }

        twai_event_callbacks_t callbacks = {};
        callbacks.on_rx_done = &Impl::onReceive;
        callbacks.on_tx_done = &Impl::onTransmitDone;
        callbacks.on_error = &Impl::onError;
        callbacks.on_state_change = &Impl::onStateChange;
        result =
            twai_node_register_event_callbacks(_node, &callbacks, this);
        if (result != ESP_OK) {
          setError(result);
          return result;
        }

        _startedUs = esp_timer_get_time();
        result = twai_node_enable(_node);
        if (result != ESP_OK) {
          setError(result);
          return result;
        }
        _canEnabled.store(true);
        _canState.store(TWAI_ERROR_ACTIVE);
        _stage.store(SessionStage::Prearmed);
        _nextPollUs = _startedUs + ColdBootDelayUs;
        _running.store(true);

        if (xTaskCreate([](void* self) { static_cast<Impl*>(self)->run(); },
                        "m/lfbros", 5120, this, 4, &_task) != pdPASS) {
          _running.store(false);
          setError(ESP_ERR_NO_MEM);
          return ESP_ERR_NO_MEM;
        }

        ESP_LOGI(Tag,
                 "TWAI prearmed: TX=GPIO%d RX=GPIO%d target=%lu actual=%lu "
                 "bit/s at %lld us",
                 static_cast<int>(_options.hardware.txPin),
                 static_cast<int>(_options.hardware.rxPin),
                 static_cast<unsigned long>(NominalBitrate),
                 static_cast<unsigned long>(ActualBitrate),
                 static_cast<long long>(_startedUs));
        updateComponents();
        return ESP_OK;
      }

      Desired desired() const {
        std::lock_guard guard(_mutex);
        return _desired;
      }
      Desired effectiveDesired() const {
        std::lock_guard control(_controlMutex);
        return _thermostatOverride ? _thermostatDesired : desired();
      }
      void manualTakeover() {
        // Call while holding _controlMutex. Queued supervisor requests carry
        // this generation and are discarded before any CAN transmission.
        ++_manualRevision;
        _thermostatRestorePending |= _thermostatOverride;
        _thermostatOverride = false;
        _thermostatPending = false;
        _thermostatUncertain = false;
      }
      thermostat::Capabilities thermostatCapabilities() const {
        thermostat::Capabilities result;
        result.startStop = result.nativeTarget = result.power = true;
        result.nativeStandby = result.externalRequiresShutdown = true;
        result.nativeTemperature = _options.thermostatNativeTemperatureQualified;
        return result;
      }
      thermostat::Feedback thermostatSnapshot() const {
        std::lock_guard control(_controlMutex);
        Runtime runtime;
        { std::lock_guard guard(_mutex); runtime = _runtime; }
        const auto now = esp_timer_get_time();
        thermostat::Feedback result;
        result.revision = _manualRevision;
        result.sampledAtMs = runtime.receivedUs / 1000;
        // Partial snapshots are useful diagnostics, never control authority.
        result.valid = runtime.valid && !runtime.partial && !runtimeIsStale(runtime, now) &&
            _connected.load() && _sourceSelected.load();
        result.enabled = !_priming.load() && !_normalModeRestorePending.load();
        result.fault = runtime.faultActive || runtime.state == 31;
        result.commandPending = _thermostatPending;
        result.commandUncertain = _thermostatUncertain;
        if (result.fault) result.phase = thermostat::Phase::Fault;
        else if (!result.valid) result.phase = thermostat::Phase::Unknown;
        else if (runtime.state == 1) result.phase = thermostat::Phase::Off;
        else if (runtime.state == 15 || runtime.state == 16) result.phase = thermostat::Phase::Standby;
        else if ((runtime.state >= 17 && runtime.state <= 20) || runtime.state == 28 || runtime.state == 30)
          result.phase = thermostat::Phase::Cooling;
        else if (runtime.state == 13) result.phase = thermostat::Phase::Heating;
        else result.phase = thermostat::Phase::Starting;
        const bool standby = runtime.state == 15 || runtime.state == 16;
        result.minimumKnown = standby || (std::isfinite(_options.thermostatLowPumpHz) &&
            _options.thermostatLowPumpHz > 0);
        result.atMinimum = result.valid && (standby ||
            (runtime.state == 13 && result.minimumKnown &&
             std::isfinite(runtime.pumpFrequencyHz) && runtime.pumpFrequencyHz > 0 &&
             runtime.pumpFrequencyHz <= _options.thermostatLowPumpHz));
        result.nativeTemperature = {runtime.ambientC, uint64_t(runtime.receivedUs / 1000),
            result.valid && _options.thermostatNativeTemperatureQualified && std::isfinite(runtime.ambientC)};
        return result;
      }
      thermostat::Result thermostatApply(const thermostat::Request& request) {
        using namespace thermostat;
        std::lock_guard control(_controlMutex);
        if (request.expectedRevision != _manualRevision) return Result::Rejected;
        if (request.command == thermostat::Command::Release) {
          const bool changed = _thermostatOverride;
          _thermostatOverride = false;
          _thermostatRestorePending |= changed;
          // Invalidate queued pulses while preserving an already sent outcome.
          ++_thermostatLease;
          if (!_thermostatSentUs) _thermostatPending = false;
          if (changed) enqueue(CommandType::ApplySetpoint);
          return Result::Accepted;
        }
        const auto now = esp_timer_get_time();
        if (!request.validUntilMs || uint64_t(now / 1000) >= request.validUntilMs) return Result::Rejected;
        if (!_connected.load() || !_sourceSelected.load()) return Result::Unavailable;
        Runtime runtime;
        { std::lock_guard guard(_mutex); runtime = _runtime; }
        if (!runtime.valid || runtime.partial || runtimeIsStale(runtime, now)) return Result::Unavailable;
        if (_priming.load() || _normalModeRestorePending.load() ||
            ((runtime.faultActive || runtime.state == 31) && request.command != thermostat::Command::Stop))
          return Result::Rejected;
        if (_thermostatUncertain) return Result::Rejected;
        if (_thermostatPending) return Result::Busy;
        CommandType type = CommandType::ApplySetpoint;
        auto target = effectiveDesired();
        if (request.command == thermostat::Command::Start) {
          if (runtime.state != 1 || _commandedOn.load()) return Result::Busy;
          type = CommandType::TurnOn;
        } else if (request.command == thermostat::Command::Stop) {
          if (runtime.state == 1 || runtime.state == 30 || runtime.state == 31 ||
              (runtime.state >= 17 && runtime.state <= 20)) return Result::Busy;
          type = CommandType::TurnOff;
        } else if (request.command == thermostat::Command::NativeTarget) {
          if (std::isfinite(request.ambientC)) return Result::Unsupported;
          if (!std::isfinite(request.targetC) || request.targetC < 10 || request.targetC > 35) return Result::Invalid;
          target.controlMode = ControlMode::Temperature;
          target.temperatureC = request.targetC;
        } else if (request.command == thermostat::Command::Power) {
          if (!std::isfinite(request.powerFraction) || request.powerFraction < 0 || request.powerFraction > 1)
            return Result::Invalid;
          target.controlMode = ControlMode::Power;
          target.powerW = _options.minimumPowerW + request.powerFraction *
              (_options.maximumPowerW - _options.minimumPowerW);
        } else return Result::Unsupported;
        const auto result = enqueue(type, true, request.validUntilMs);
        if (result != ESP_OK) return Result::Busy;
        _thermostatValidUntilMs = request.validUntilMs;
        if (type == CommandType::ApplySetpoint) {
          _thermostatDesired = target;
          _thermostatOverride = true;
        } else {
          _thermostatPending = true;
          _thermostatPulse = type;
          _thermostatSentUs = 0;
        }
        return Result::Accepted;
      }
      void setThermostatAttached(bool attached) {
        _thermostatAttached.store(attached);
        _climate.setDisabled(attached);
        _powerSetpoint.setDisabled(attached);
      }
      void expireThermostat() {
        std::lock_guard control(_controlMutex);
        if (_thermostatOverride && uint64_t(esp_timer_get_time() / 1000) >= _thermostatValidUntilMs) {
          _thermostatOverride = false;
          _thermostatRestorePending = true;
          // Restore the user's saved native setting; this is not a shutdown
          // guarantee when the supervisory control lease has disappeared.
          enqueue(CommandType::ApplySetpoint);
        }
      }
      void observeThermostat(const Runtime& runtime) {
        std::lock_guard control(_controlMutex);
        if (!_thermostatPending || !_thermostatSentUs || runtime.partial ||
            runtime.receivedUs <= _thermostatSentUs) return;
        const bool observed = _thermostatPulse == CommandType::TurnOn ?
            ((runtime.state >= 2 && runtime.state <= 16) || runtime.state == 29) :
            (runtime.state == 1 || (runtime.state >= 17 && runtime.state <= 20) || runtime.state == 30);
        if (observed) _thermostatPending = false;
      }

      bool setDesired(Desired desired) {
        std::lock_guard control(_controlMutex);
        const bool hadOverride = _thermostatOverride;
        manualTakeover();
        desired.temperatureC = std::clamp(desired.temperatureC, 10.0f, 35.0f);
        desired.powerW = std::clamp(desired.powerW, _options.minimumPowerW,
                                    _options.maximumPowerW);
        bool changed;
        {
          std::lock_guard guard(_mutex);
          changed = _desired.controlMode != desired.controlMode ||
                    _desired.temperatureC != desired.temperatureC ||
                    _desired.powerW != desired.powerW;
          _desired = desired;
        }
        if (changed || hadOverride) {
          updateComponents();
          enqueue(CommandType::ApplySetpoint);
        }
        return changed;
      }

      esp_err_t setTemperature(float value) {
        std::lock_guard control(_controlMutex);
        if (!std::isfinite(value) || value < 10.0f || value > 35.0f)
          return ESP_ERR_INVALID_ARG;
        auto valueState = desired();
        valueState.temperatureC = value;
        valueState.controlMode = ControlMode::Temperature;
        if (setDesired(valueState))
          config::Changed::publish(_owner);
        return ESP_OK;
      }

      esp_err_t setPower(float value) {
        std::lock_guard control(_controlMutex);
        if (!std::isfinite(value) || value < _options.minimumPowerW ||
            value > _options.maximumPowerW)
          return ESP_ERR_INVALID_ARG;
        auto valueState = desired();
        valueState.powerW = value;
        valueState.controlMode = ControlMode::Power;
        if (setDesired(valueState))
          config::Changed::publish(_owner);
        return ESP_OK;
      }

      esp_err_t setControlMode(ControlMode mode) {
        std::lock_guard control(_controlMutex);
        auto valueState = desired();
        if (valueState.controlMode == mode) {
          const bool hadOverride = _thermostatOverride;
          manualTakeover();
          if (hadOverride) enqueue(CommandType::ApplySetpoint);
          return ESP_OK;
        }
        valueState.controlMode = mode;
        if (setDesired(valueState))
          config::Changed::publish(_owner);
        return ESP_OK;
      }

      esp_err_t turn(bool on) {
        std::lock_guard control(_controlMutex);
        if (!_connected.load() || !_sourceSelected.load())
          return ESP_ERR_INVALID_STATE;
        if (on && (_priming.load() || _normalModeRestorePending.load()))
          return ESP_ERR_INVALID_STATE;
        const auto result = enqueue(on ? CommandType::TurnOn : CommandType::TurnOff);
        if (result == ESP_OK) manualTakeover();
        return result;
      }

      esp_err_t clearFault() {
        std::lock_guard control(_controlMutex);
        if (!_connected.load() || !_sourceSelected.load())
          return ESP_ERR_INVALID_STATE;
        Runtime runtime;
        {
          std::lock_guard guard(_mutex);
          runtime = _runtime;
        }
        if (!runtime.valid || runtime.state != 31 || !runtime.faultActive)
          return ESP_ERR_INVALID_STATE;
        const auto result = enqueue(CommandType::ClearFault);
        if (result == ESP_OK) manualTakeover();
        return result;
      }

      esp_err_t startPriming() {
        std::lock_guard control(_controlMutex);
        if (!_connected.load() || !_sourceSelected.load() ||
            _commandedOn.load() || _priming.load() ||
            _normalModeRestorePending.load())
          return ESP_ERR_INVALID_STATE;
        Runtime runtime;
        {
          std::lock_guard guard(_mutex);
          runtime = _runtime;
        }
        if (!runtime.valid || runtime.state != 1 || runtime.faultActive)
          return ESP_ERR_INVALID_STATE;
        const auto result = enqueue(CommandType::StartPriming);
        if (result == ESP_OK) manualTakeover();
        return result;
      }

      float minimumPower() const {
        return _options.minimumPowerW;
      }

      float maximumPower() const {
        return _options.maximumPowerW;
      }

      void toState(JsonObject root) const {
        const auto desiredState = desired();
        Runtime runtime;
        {
          std::lock_guard guard(_mutex);
          runtime = _runtime;
        }
        const auto now = esp_timer_get_time();
        const bool telemetryStale = runtimeIsStale(runtime, now);
        const bool mode1Pumping =
            runtime.valid && runtime.state == 23 && _priming.load() &&
            _commandedOn.load();

        root["connected"] = _connected.load();

        auto session = root["session"].to<JsonObject>();
        session["stage"] = stageName(_stage.load());
        session["applicationSeen"] = _applicationSeen.load();
        session["sourceSelected"] = _sourceSelected.load();
        session["startedUs"] = _startedUs;
        session["lastApplicationResponseUs"] = _lastApplicationResponseUs.load();
        session["lastValidRuntimeUs"] = _lastValidRuntimeUs.load();

        auto command = root["command"].to<JsonObject>();
        command["on"] = _commandedOn.load();
        command["operationMode"] =
            _operationMode.load() == 1 ? "polr" : "normal";
        command["priming"] = _priming.load();
        command["operationModeRestorePending"] =
            _normalModeRestorePending.load();
        command["controlMode"] = controlModeName(desiredState.controlMode);
        command["temperatureSetpointC"] = desiredState.temperatureC;
        command["powerSetpointW"] = desiredState.powerW;
        auto limits = command["limits"].to<JsonObject>();
        limits["minimumTemperatureC"] = 10;
        limits["maximumTemperatureC"] = 35;
        limits["minimumPowerW"] = _options.minimumPowerW;
        limits["maximumPowerW"] = _options.maximumPowerW;

        auto heater = root["heater"].to<JsonObject>();
        heater["valid"] = runtime.valid;
        heater["stale"] = telemetryStale;
        if (runtime.valid)
          heater["telemetrySampledAtMs"] = runtime.receivedUs / 1000;
        if (runtime.valid) {
          heater["firmwareTag"] = runtime.firmwareTag;
          heater["partial"] = runtime.partial;
          heater["chunkMask"] = runtime.chunkMask;
          auto state = heater["state"].to<JsonObject>();
          state["code"] = runtime.state;
          state["name"] = heaterStateName(runtime.state);
          state["group"] = heaterStateGroup(runtime.state);
          state["nextCode"] = runtime.nextState;
          state["nextName"] = heaterStateName(runtime.nextState);
          state["nextGroup"] = heaterStateGroup(runtime.nextState);
          state["thermalSubstate"] = runtime.thermalSubstate;
          state["fanControlState"] = runtime.fanControlState;
          state["fanControlNextState"] = runtime.fanControlNextState;

          auto fault = heater["fault"].to<JsonObject>();
          fault["active"] = runtime.faultActive;
          fault["displayCode"] = runtime.faultCode;
          auto flags = fault["flags"].to<JsonObject>();
          char key[5];
          for (int index = 0; index < 10; index++) {
            snprintf(key, sizeof(key), "0x%02X", 0x48 + index);
            flags[key] = runtime.faultFlags[index];
          }

          auto display = heater["physicalDisplay"].to<JsonObject>();
          display["operationMode"] = runtime.displayOperationMode;
          display["operationModeName"] =
              operationModeName(runtime.displayOperationMode);
          display["controlMode"] = runtime.displayControlMode;
          display["controlModeName"] =
              runtime.displayControlMode == 0 ? "temperature" : "power";
          display["specialTarget"] = runtime.displaySpecialTarget;
          json::to(display, "temperatureSetpointC",
                   runtime.displayTemperatureC);
          json::to(display, "powerSetpointW", runtime.displayPowerW);
          display["onOffCommand"] = runtime.displayOnOff;

          auto pump = heater["pump"].to<JsonObject>();
          pump["on"] = mode1Pumping ||
                       (std::isfinite(runtime.pumpFrequencyHz) &&
                        runtime.pumpFrequencyHz > 0.05f);
          pump["mode"] =
              mode1Pumping ? "mode1-pumping" : "normal-scheduler";
          pump["mode1Pumping"] = mode1Pumping;
          pump["frequencyAvailable"] = !mode1Pumping;
          if (mode1Pumping)
            pump["frequencyHz"] = nullptr;
          else
            json::to(pump, "frequencyHz", runtime.pumpFrequencyHz);
          pump["pulseCount"] = runtime.pumpPulseCount;

          auto glow = heater["glow"].to<JsonObject>();
          glow["on"] =
              std::isfinite(runtime.glowDuty) && runtime.glowDuty > 0.005f;
          json::to(glow, "dutyFraction", runtime.glowDuty);
          if (std::isfinite(runtime.glowDuty))
            glow["dutyPercent"] = runtime.glowDuty * 100.0f;
          json::to(glow, "effectiveResistanceOhm",
                   runtime.glowResistanceOhm);
          json::to(glow, "voltageV", runtime.glowVoltageV);
          json::to(glow, "minimumVoltageV", runtime.glowMinimumVoltageV);
          json::to(glow, "estimatedPowerW", runtime.glowPowerW);

          auto fan = heater["fan"].to<JsonObject>();
          fan["on"] = runtime.fanRpm > 0;
          fan["speedRpm"] = runtime.fanRpm;
          json::to(fan, "driveFraction", runtime.fanDrive);
          fan["targetRpm"] = runtime.fanTargetRpm;

          auto temperature = heater["temperature"].to<JsonObject>();
          json::to(temperature, "sensorA_C", runtime.sensorA_C);
          json::to(temperature, "sensorB_C", runtime.sensorB_C);
          json::to(temperature, "chamberC", runtime.chamberC);
          json::to(temperature, "ambientC", runtime.ambientC);

          auto electrical = heater["electrical"].to<JsonObject>();
          json::to(electrical, "supplyVoltage", runtime.supplyVoltageV);
          heater["receivedUs"] = runtime.receivedUs;
        }

        auto can = root["can"].to<JsonObject>();
        can["enabled"] = _canEnabled.load();
        can["state"] = twaiStateName(_canState.load());
        can["nominalBitrate"] = NominalBitrate;
        can["actualBitrate"] = ActualBitrate;
        can["rxFrames"] = _rxFrames.load();
        can["rxDropped"] = _rxDropped.load();
#if CONFIG_IDF_TARGET_ESP32S3
        can["hardwareRxDiagnostics"] = true;
#else
        can["hardwareRxDiagnostics"] = false;
#endif
        can["maxHardwareRxPending"] = _maxHardwareRxPending.load();
        can["hardwareRxMissEvents"] = _hardwareRxMissEvents.load();
        can["hardwareDataOverrunEvents"] =
            _hardwareDataOverrunEvents.load();
        can["runtimeRequests"] = _runtimeRequests.load();
        can["validSnapshots"] = _validSnapshots.load();
        can["partialSnapshots"] = _partialSnapshots.load();
        can["invalidSnapshots"] = _invalidSnapshots.load();
        can["inferredMissingChunks"] = _inferredMissingChunks.load();
        can["maxInterFrameGapUs"] = _maxInterFrameGapUs.load();
        can["txAttempts"] = _txAttempts.load();
        can["txSucceeded"] = _txSucceededCount.load();
        can["txFailed"] = _txFailedCount.load();
        can["busErrors"] = _busErrors.load();
        can["arbitrationLostErrors"] = _arbitrationLostErrors.load();
        can["bitErrors"] = _bitErrors.load();
        can["formErrors"] = _formErrors.load();
        can["stuffErrors"] = _stuffErrors.load();
        can["ackErrors"] = _ackErrors.load();
        can["errorEvents"] = _errorEvents.load();
        can["unclassifiedErrorEvents"] =
            _unclassifiedErrorEvents.load();
        can["errorBursts"] = _errorBursts.load();
        can["lastErrorEventUs"] = _lastErrorEventUs.load();
        can["lastErrorBurstStartUs"] = _lastErrorBurstStartUs.load();
        can["lastErrorBurstEndUs"] = _lastErrorBurstEndUs.load();
        can["lastErrorBurstEvents"] = _lastErrorBurstEvents.load();
        can["lastErrorBurstFlags"] = _lastErrorBurstFlags.load();
        can["lastErrorBurstRequest"] = _lastErrorBurstRequest.load();
        can["lastErrorBurstOffsetFromRequestUs"] =
            _lastErrorBurstOffsetFromRequestUs.load();
        can["lastErrorHardwareStatus"] = _lastErrorHardwareStatus.load();
        can["lastErrorRxPending"] = _lastErrorRxPending.load();
        can["lastErrorBurstHardwareStatus"] =
            _lastErrorBurstHardwareStatus.load();
        can["lastErrorBurstMaxRxPending"] =
            _lastErrorBurstMaxRxPending.load();
        can["txErrorCount"] = _txErrorCount.load();
        can["rxErrorCount"] = _rxErrorCount.load();
        can["lastError"] = _lastError.load();
      }

      void describeHa(Request& request) {
        if (!_thermostatAttached.load()) {
          JsonDocument doc;
          auto root = doc.to<JsonObject>();
          root["id"] = _climate.uid();
          root["component"] = "climate";
          root["componentId"] = _climate.id();
          root["has_state"] = true;
          root["accepts_commands"] = true;
          auto stateTopics = root["stateTopicNames"].to<JsonArray>();
          stateTopics.add("mode_state_topic");
          stateTopics.add("action_topic");
          stateTopics.add("temperature_state_topic");
          stateTopics.add("current_temperature_topic");
          auto commandTopics = root["commandTopicNames"].to<JsonArray>();
          commandTopics.add("mode_command_topic");
          commandTopics.add("temperature_command_topic");
          commandTopics.add("power_command_topic");
          auto config = root["config"].to<JsonObject>();
          config["name"] = "Heater";
          config["platform"] = "climate";
          auto modes = config["modes"].to<JsonArray>();
          modes.add("off");
          modes.add("heat");
          config["mode_state_template"] =
              "{{ value_json['climate']['mode'] }}";
          config["action_template"] =
              "{{ value_json['climate']['action'] }}";
          config["temperature_state_template"] =
              "{{ value_json['climate']['temperature'] }}";
          config["current_temperature_template"] =
              "{{ value_json['climate']['currentTemperature'] }}";
          config["mode_command_template"] =
              "{\"mode\":\"{{ value }}\"}";
          config["temperature_command_template"] =
              "{\"temperature\":{{ value }}}";
          config["payload_on"] = "{\"on\":true}";
          config["payload_off"] = "{\"on\":false}";
          config["temperature_unit"] = "C";
          config["min_temp"] = 10;
          config["max_temp"] = 35;
          config["temp_step"] = 1;
          config["precision"] = 1;
          request.respond(_owner->name(), doc.as<JsonVariantConst>());
        }
        if (!_thermostatAttached.load()) {
          JsonDocument doc;
          auto root = doc.to<JsonObject>();
          root["id"] = _powerSetpoint.uid();
          root["component"] = "number";
          root["componentId"] = _powerSetpoint.id();
          root["has_state"] = true;
          root["accepts_commands"] = true;
          auto config = root["config"].to<JsonObject>();
          config["name"] = "Power setpoint";
          config["platform"] = "number";
          config["min"] = _options.minimumPowerW;
          config["max"] = _options.maximumPowerW;
          config["step"] = 100;
          config["mode"] = "slider";
          config["unit_of_measurement"] = "W";
          config["device_class"] = "power";
          config["value_template"] = "{{ value_json['power_setpoint'] }}";
          request.respond(_owner->name(), doc.as<JsonVariantConst>());
        }
      }

      bool handleHaCommand(integrations::ha::CommandRequest& request) {
        auto payload = request.data().as<const char*>();
        if (!payload)
          return false;

        if (request.componentId == _climate.id()) {
          if (_thermostatAttached.load()) return false;
          JsonDocument doc;
          auto error = deserializeJson(doc, payload);
          if (error) {
            if (!strcasecmp(payload, "heat"))
              return turn(true) == ESP_OK;
            if (!strcasecmp(payload, "off"))
              return turn(false) == ESP_OK;
            return false;
          }
          auto root = doc.as<JsonObjectConst>();
          if (root["on"].is<bool>())
            return turn(root["on"].as<bool>()) == ESP_OK;
          auto mode = root["mode"].as<const char*>();
          if (mode)
            return turn(!strcasecmp(mode, "heat")) == ESP_OK;
          if (root["temperature"].is<float>() ||
              root["temperature"].is<int>())
            return setTemperature(root["temperature"].as<float>()) == ESP_OK;
          return false;
        }

        if (request.componentId == _powerSetpoint.id()) {
          char* end = nullptr;
          const auto value = strtof(payload, &end);
          return end && end != payload && setPower(value) == ESP_OK;
        }

        if (request.componentId == _controlMode.id()) {
          ControlMode mode;
          return parseControlMode(payload, mode) &&
                 setControlMode(mode) == ESP_OK;
        }
        return false;
      }

     private:
      Lfbros* _owner;
      Options _options;
      mutable std::mutex _mutex;
      mutable std::recursive_mutex _controlMutex;
      uint64_t _manualRevision = 1, _thermostatLease = 1;
      uint64_t _thermostatValidUntilMs = 0;
      int64_t _thermostatSentUs = 0;
      bool _thermostatOverride = false, _thermostatPending = false, _thermostatUncertain = false;
      bool _thermostatRestorePending = false;
      CommandType _thermostatPulse = CommandType::TurnOff;
      Desired _thermostatDesired;
      std::atomic<bool> _thermostatAttached = false;
      Desired _desired;
      Runtime _runtime;

      ClimateComponent _climate;
      NumberComponent _powerSetpoint;
      Select _controlMode;
      BinarySensor _connectedSensor;
      BinarySensor _faultSensor;
      Sensor _stateSensor;
      Sensor _chamberSensor;
      Sensor _ambientSensor;
      Sensor _voltageSensor;
      Sensor _fanSensor;
      Sensor _pumpSensor;
      Sensor _glowPowerSensor;
      Sensor _faultCodeSensor;

      twai_node_handle_t _node = nullptr;
      QueueHandle_t _rxQueue = nullptr;
      QueueHandle_t _commandQueue = nullptr;
      SemaphoreHandle_t _txDone = nullptr;
      TaskHandle_t _task = nullptr;
      twai_frame_t _txFrame = {};
      uint8_t _txData[8] = {};
      std::atomic<bool> _txInFlight = false;
      std::atomic<bool> _txCompletionSucceeded = false;
      std::atomic<bool> _running = false;
      std::atomic<bool> _canEnabled = false;
      std::atomic<int> _canState = TWAI_ERROR_BUS_OFF;
      std::atomic<SessionStage> _stage = SessionStage::Starting;
      std::atomic<bool> _applicationSeen = false;
      std::atomic<bool> _sourceSelected = false;
      std::atomic<bool> _commandedOn = false;
      std::atomic<uint8_t> _operationMode = 0;
      std::atomic<bool> _priming = false;
      std::atomic<bool> _normalModeRestorePending = false;
      std::atomic<bool> _connected = false;
      std::atomic<int32_t> _lastError = ESP_OK;
      std::atomic<int64_t> _lastApplicationResponseUs = 0;
      std::atomic<int64_t> _lastValidRuntimeUs = 0;

      std::atomic<uint32_t> _rxFrames = 0;
      std::atomic<uint32_t> _rxDropped = 0;
      std::atomic<uint32_t> _maxHardwareRxPending = 0;
      std::atomic<uint32_t> _hardwareRxMissEvents = 0;
      std::atomic<uint32_t> _hardwareDataOverrunEvents = 0;
      std::atomic<uint32_t> _runtimeRequests = 0;
      std::atomic<uint32_t> _validSnapshots = 0;
      std::atomic<uint32_t> _partialSnapshots = 0;
      std::atomic<uint32_t> _invalidSnapshots = 0;
      std::atomic<uint32_t> _inferredMissingChunks = 0;
      std::atomic<uint64_t> _maxInterFrameGapUs = 0;
      std::atomic<uint32_t> _txAttempts = 0;
      std::atomic<uint32_t> _txSucceededCount = 0;
      std::atomic<uint32_t> _txFailedCount = 0;
      std::atomic<uint32_t> _busErrors = 0;
      std::atomic<uint32_t> _arbitrationLostErrors = 0;
      std::atomic<uint32_t> _bitErrors = 0;
      std::atomic<uint32_t> _formErrors = 0;
      std::atomic<uint32_t> _stuffErrors = 0;
      std::atomic<uint32_t> _ackErrors = 0;
      std::atomic<uint32_t> _errorEvents = 0;
      std::atomic<uint32_t> _unclassifiedErrorEvents = 0;
      std::atomic<uint32_t> _errorBursts = 0;
      std::atomic<uint64_t> _lastErrorEventUs = 0;
      std::atomic<uint64_t> _lastErrorBurstStartUs = 0;
      std::atomic<uint64_t> _lastErrorBurstEndUs = 0;
      std::atomic<uint32_t> _lastErrorBurstEvents = 0;
      std::atomic<uint32_t> _lastErrorBurstFlags = 0;
      std::atomic<uint32_t> _lastErrorBurstRequest = 0;
      std::atomic<int64_t> _lastErrorBurstOffsetFromRequestUs = -1;
      std::atomic<uint32_t> _lastErrorHardwareStatus = 0;
      std::atomic<uint32_t> _lastErrorRxPending = 0;
      std::atomic<uint32_t> _lastErrorBurstHardwareStatus = 0;
      std::atomic<uint32_t> _lastErrorBurstMaxRxPending = 0;
      std::atomic<uint16_t> _txErrorCount = 0;
      std::atomic<uint16_t> _rxErrorCount = 0;
      std::atomic<int64_t> _lastRuntimeRequestUs = 0;

      int64_t _startedUs = 0;
      int64_t _nextPollUs = 0;
      int64_t _applicationSeenUs = 0;
      int64_t _lastStatusUs = 0;
      int64_t _normalModeRestoreRequestedUs = 0;
      bool _recoveryRequested = false;

      bool _assembling = false;
      uint32_t _pendingCrc = 0;
      bool _pendingCrcPresent = false;
      uint64_t _headerUs = 0;
      uint32_t _payloadCrc = 0;
      bool _payloadCrcPresent = false;
      size_t _payloadChunks = 0;
      size_t _payloadLength = 0;
      size_t _logicalChunkIndex = 0;
      uint32_t _payloadChunkMask = 0;
      uint64_t _previousChunkTimestampUs = 0;
      uint8_t _payload[RuntimeLength] = {};

      void setError(esp_err_t error) {
        _lastError.store(error);
        _stage.store(SessionStage::Error);
      }

      esp_err_t enqueue(CommandType type, bool thermostat = false, uint64_t validUntilMs = 0) {
        if (!_commandQueue)
          return ESP_ERR_INVALID_STATE;
        Command command = {.type = type, .thermostat = thermostat,
            .revision = _manualRevision, .validUntilMs = validUntilMs, .lease = _thermostatLease};
        return xQueueSend(_commandQueue, &command, 0) == pdTRUE
                   ? ESP_OK
                   : ESP_ERR_TIMEOUT;
      }

      void run() {
        while (_running.load()) {
          drainRx();
          updateConnection();
          recoverCan();
          expireThermostat();
          processCommands();
          restoreNormalModeWhenSafe();
          processSession();
          snapshotCanStatus();
          vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(5)));
        }
        vTaskDelete(nullptr);
      }

      void processSession() {
        std::lock_guard control(_controlMutex);
        if (!_canEnabled.load())
          return;
        const auto now = esp_timer_get_time();

        if (!_applicationSeen.load()) {
          if (now >= _nextPollUs) {
            _stage.store(SessionStage::Probing);
            requestRuntime();
            _nextPollUs = now + nextPollDelayUs();
            publishState();
          }
          return;
        }

        if (!_sourceSelected.load()) {
          if (now - _applicationSeenUs < ClaimDelayUs)
            return;
          _stage.store(SessionStage::Claiming);
          if (claimControlOff() == ESP_OK) {
            _sourceSelected.store(true);
            _commandedOn.store(false);
            _stage.store(SessionStage::Active);
            applyDesiredSetpoint();
          }
          _applicationSeenUs = now;
          publishState();
          return;
        }

        if (now >= _nextPollUs) {
          requestRuntime();
          _nextPollUs = now + nextPollDelayUs();
        }
      }

      int64_t nextPollDelayUs() const {
        auto delayUs =
            static_cast<int64_t>(_options.pollIntervalMs) * 1000;
        if (_priming.load() && _commandedOn.load()) {
          // Mode 1's pump strokes can be synchronous with the default
          // one-second request interval. Walk requests through several
          // non-harmonic phases so at least some complete runtime blocks can
          // land between the pump's noisy edges.
          constexpr int64_t JitterUs[] = {73'000, 137'000, 211'000, 283'000,
                                          359'000};
          delayUs = std::max<int64_t>(delayUs, 2'000'000);
          if (_canState.load() != TWAI_ERROR_ACTIVE)
            delayUs += 1'000'000;
          delayUs += JitterUs[_runtimeRequests.load() % 5];
        }
        return delayUs;
      }

      bool runtimeIsStale(const Runtime& runtime, int64_t now) const {
        const auto staleAfterUs = std::max<int64_t>(
            3'000'000,
            static_cast<int64_t>(_options.pollIntervalMs) * 3 * 1000);
        return runtime.valid && runtime.receivedUs > 0 &&
               now - runtime.receivedUs > staleAfterUs;
      }

      void processCommands() {
        if (!_commandQueue)
          return;
        Command command;
        while (xQueueReceive(_commandQueue, &command, 0) == pdTRUE) {
          std::lock_guard control(_controlMutex);
          if (command.thermostat && (command.revision != _manualRevision ||
              command.lease != _thermostatLease ||
              uint64_t(esp_timer_get_time() / 1000) >= command.validUntilMs)) {
            if (command.revision == _manualRevision && command.lease == _thermostatLease)
              _thermostatPending = false;
            continue;
          }
          if (command.thermostat) {
            Runtime runtime;
            { std::lock_guard guard(_mutex); runtime = _runtime; }
            const bool safe = _connected.load() && _sourceSelected.load() && runtime.valid &&
                !runtime.partial && !runtimeIsStale(runtime, esp_timer_get_time()) &&
                (command.type == CommandType::TurnOff || (!runtime.faultActive && runtime.state != 31)) && !_priming.load() &&
                !_normalModeRestorePending.load() &&
                (command.type != CommandType::TurnOn || (runtime.state == 1 && !_commandedOn.load()));
            if (!safe) { _thermostatPending = false; continue; }
          }
          esp_err_t result = ESP_ERR_INVALID_STATE;
          switch (command.type) {
            case CommandType::TurnOn:
              if (_connected.load() && _sourceSelected.load()) {
                const uint8_t source[] = {4, 1};
                const uint8_t mode[] = {2, 0};
                const uint8_t on[] = {3, 1};
                result = command.thermostat || _thermostatRestorePending ? applyDesiredSetpoint() : ESP_OK;
                if (result == ESP_OK)
                  result = sendFrame(ControlIdentifier, source, sizeof(source));
                if (result == ESP_OK)
                  result = sendFrame(ControlIdentifier, mode, sizeof(mode));
                if (result == ESP_OK) {
                  _operationMode.store(0);
                  _priming.store(false);
                  _normalModeRestorePending.store(false);
                  _normalModeRestoreRequestedUs = 0;
                }
                if (result == ESP_OK && command.thermostat &&
                    uint64_t(esp_timer_get_time() / 1000) >= command.validUntilMs)
                  result = ESP_ERR_INVALID_STATE;
                if (result == ESP_OK)
                  result = sendFrame(ControlIdentifier, on, sizeof(on));
                if (result == ESP_OK) {
                  _sourceSelected.store(true);
                  _commandedOn.store(true);
                }
              }
              break;
            case CommandType::TurnOff:
              if (_connected.load() && _sourceSelected.load()) {
                const uint8_t off[] = {3, 0};
                result = sendFrame(ControlIdentifier, off, sizeof(off));
                if (result == ESP_OK) {
                  _commandedOn.store(false);
                  if (_operationMode.load() == 1) {
                    _normalModeRestoreRequestedUs = esp_timer_get_time();
                    _normalModeRestorePending.store(true);
                  }
                }
              }
              break;
            case CommandType::ClearFault:
              if (_connected.load()) {
                Runtime runtime;
                {
                  std::lock_guard guard(_mutex);
                  runtime = _runtime;
                }
                if (runtime.valid && runtime.state == 31 &&
                    runtime.faultActive) {
                  result =
                      sendFrame(ControllerResetIdentifier, nullptr, 0);
                  if (result == ESP_OK)
                    resetSessionAfterControllerReset();
                }
              }
              break;
            case CommandType::StartPriming:
              if (_connected.load() && _sourceSelected.load() &&
                  !_commandedOn.load() && isStandbyOff()) {
                const uint8_t source[] = {4, 1};
                const uint8_t mode[] = {2, 1};
                const uint8_t target[] = {5, 0xF4, 0x01, 0, 0};
                const uint8_t on[] = {3, 1};
                result = sendFrame(ControlIdentifier, source, sizeof(source));
                if (result == ESP_OK)
                  result = sendFrame(ControlIdentifier, mode, sizeof(mode));
                if (result == ESP_OK) {
                  _operationMode.store(1);
                  _priming.store(true);
                }
                if (result == ESP_OK)
                  result =
                      sendFrame(ControlIdentifier, target, sizeof(target));
                if (result == ESP_OK)
                  result = sendFrame(ControlIdentifier, on, sizeof(on));
                if (result == ESP_OK) {
                  _sourceSelected.store(true);
                  _commandedOn.store(true);
                } else if (_operationMode.load() == 1) {
                  _normalModeRestoreRequestedUs = esp_timer_get_time();
                  _normalModeRestorePending.store(true);
                }
              }
              break;
            case CommandType::ApplySetpoint:
              if (_sourceSelected.load())
                result = applyDesiredSetpoint();
              else
                result = ESP_OK;  // applied automatically after safe claim
              break;
          }
          if (result != ESP_OK)
            _lastError.store(result);
          if (command.thermostat) {
            if (result != ESP_OK) {
              _thermostatPending = false;
              _thermostatUncertain = true;
            } else if (command.type == CommandType::TurnOn || command.type == CommandType::TurnOff)
              _thermostatSentUs = esp_timer_get_time();
          }
          publishState();
        }
      }

      bool isStandbyOff() const {
        std::lock_guard guard(_mutex);
        return _runtime.valid && !_runtime.faultActive && _runtime.state == 1;
      }

      void restoreNormalModeWhenSafe() {
        if (!_normalModeRestorePending.load() || !_connected.load() ||
            !_sourceSelected.load())
          return;

        Runtime runtime;
        {
          std::lock_guard guard(_mutex);
          runtime = _runtime;
        }
        const auto now = esp_timer_get_time();
        if (!runtime.valid || runtime.state != 1 ||
            runtime.receivedUs <= _normalModeRestoreRequestedUs ||
            now - _normalModeRestoreRequestedUs < 250'000)
          return;

        const uint8_t mode[] = {2, 0};
        const auto result =
            sendFrame(ControlIdentifier, mode, sizeof(mode));
        if (result == ESP_OK) {
          _operationMode.store(0);
          _priming.store(false);
          _normalModeRestorePending.store(false);
          _normalModeRestoreRequestedUs = 0;
          publishState();
        } else
          _lastError.store(result);
      }

      esp_err_t requestRuntime() {
        _runtimeRequests.fetch_add(1);
        _lastRuntimeRequestUs.store(esp_timer_get_time());
        return sendFrame(RuntimeRequestIdentifier, nullptr, 0);
      }

      void resetSessionAfterControllerReset() {
        std::lock_guard control(_controlMutex);
        manualTakeover();
        const auto now = esp_timer_get_time();
        _connected.store(false);
        _applicationSeen.store(false);
        _sourceSelected.store(false);
        _commandedOn.store(false);
        _operationMode.store(0);
        _priming.store(false);
        _normalModeRestorePending.store(false);
        _normalModeRestoreRequestedUs = 0;
        _lastApplicationResponseUs.store(0);
        _lastValidRuntimeUs.store(0);
        _applicationSeenUs = 0;
        _stage.store(SessionStage::Prearmed);
        _nextPollUs = now + ColdBootDelayUs;
        resetAssembler();
        {
          std::lock_guard guard(_mutex);
          _runtime = Runtime{};
        }
        updateComponents();
      }

      esp_err_t claimControlOff() {
        const uint8_t source[] = {4, 1};
        const uint8_t off[] = {3, 0};
        auto result = sendFrame(ControlIdentifier, source, sizeof(source));
        if (result == ESP_OK)
          result = sendFrame(ControlIdentifier, off, sizeof(off));
        return result;
      }

      esp_err_t applyDesiredSetpoint() {
        const auto valueState = effectiveDesired();
        uint8_t data[5];
        data[0] = valueState.controlMode == ControlMode::Temperature ? 0 : 1;
        const auto value = valueState.controlMode == ControlMode::Temperature
                               ? valueState.temperatureC
                               : valueState.powerW;
        std::memcpy(data + 1, &value, sizeof(value));
        const auto result = sendFrame(ControlIdentifier, data, sizeof(data));
        if (result == ESP_OK) _thermostatRestorePending = false;
        return result;
      }

      esp_err_t sendFrame(uint32_t id, const uint8_t* data, size_t size) {
        if (!_node || !_canEnabled.load() || size > sizeof(_txData))
          return ESP_ERR_INVALID_STATE;
        if (_txInFlight.exchange(true))
          return ESP_ERR_INVALID_STATE;

        while (xSemaphoreTake(_txDone, 0) == pdTRUE) {
        }
        std::memset(&_txFrame, 0, sizeof(_txFrame));
        std::memset(_txData, 0, sizeof(_txData));
        if (data && size)
          std::memcpy(_txData, data, size);
        _txFrame.header.id = id;
        _txFrame.header.ide = true;
        _txFrame.header.rtr = false;
        _txFrame.header.fdf = false;
        _txFrame.header.brs = false;
        _txFrame.header.dlc = static_cast<uint8_t>(size);
        _txFrame.buffer = _txData;
        _txFrame.buffer_len = size;
        _txCompletionSucceeded.store(false);
        _txAttempts.fetch_add(1);

        auto result = twai_node_transmit(_node, &_txFrame, 20);
        if (result != ESP_OK) {
          _txInFlight.store(false);
          _txFailedCount.fetch_add(1);
          _lastError.store(result);
          return result;
        }
        if (xSemaphoreTake(_txDone, pdMS_TO_TICKS(250)) != pdTRUE) {
          _txFailedCount.fetch_add(1);
          _lastError.store(ESP_ERR_TIMEOUT);
          return ESP_ERR_TIMEOUT;
        }
        if (!_txCompletionSucceeded.load()) {
          _txFailedCount.fetch_add(1);
          _lastError.store(ESP_FAIL);
          return ESP_FAIL;
        }
        _txSucceededCount.fetch_add(1);
        _lastError.store(ESP_OK);
        return ESP_OK;
      }

      void drainRx() {
        RxFrame frame;
        while (_rxQueue && xQueueReceive(_rxQueue, &frame, 0) == pdTRUE) {
          _rxFrames.fetch_add(1);
          if (frame.id != ResponseIdentifier || !frame.extended ||
              frame.remote)
            continue;
          const auto now = esp_timer_get_time();
          const auto previous = _lastApplicationResponseUs.exchange(now);
          const bool newSession = !previous || now - previous > NewSessionGapUs;
          _connected.store(true);
          if (newSession) {
            std::lock_guard control(_controlMutex);
            manualTakeover();
            _sourceSelected.store(false);
            _commandedOn.store(false);
            _operationMode.store(0);
            _priming.store(false);
            _normalModeRestorePending.store(false);
            _normalModeRestoreRequestedUs = 0;
            _applicationSeen.store(true);
            _applicationSeenUs = now;
            _stage.store(SessionStage::ApplicationSeen);
          } else if (!_applicationSeen.exchange(true)) {
            _applicationSeenUs = now;
            _stage.store(SessionStage::ApplicationSeen);
          }
          consumeRuntimeFrame(frame, now);
        }
      }

      void consumeRuntimeFrame(const RxFrame& frame, int64_t now) {
        if (frame.dlc == 5 && frame.data[0] == RuntimeChunks) {
          if (_assembling)
            finishPayload(now);
          resetAssembler();
          _pendingCrc = readU32(frame.data + 1);
          _pendingCrcPresent = true;
          _headerUs = frame.timestampUs;
          return;
        }

        if (frame.dlc == sizeof(RuntimeMagic) &&
            !std::memcmp(frame.data, RuntimeMagic, sizeof(RuntimeMagic))) {
          if (_assembling)
            finishPayload(now);
          resetPayload();
          _assembling = true;
          if (_pendingCrcPresent && frame.timestampUs >= _headerUs &&
              frame.timestampUs - _headerUs <= HeaderMaxAgeUs) {
            _payloadCrc = _pendingCrc;
            _payloadCrcPresent = true;
          }
          _pendingCrcPresent = false;
          _logicalChunkIndex = 0;
          _previousChunkTimestampUs = frame.timestampUs;
          storePayloadChunk(0, frame.data, frame.dlc);
          return;
        }

        if (!_assembling || _payloadChunks >= RuntimeChunks ||
            (frame.dlc != 8 && frame.dlc != 4))
          return;

        if (frame.timestampUs < _previousChunkTimestampUs) {
          _invalidSnapshots.fetch_add(1);
          resetPayload();
          return;
        }
        const auto gapUs = frame.timestampUs - _previousChunkTimestampUs;
        updateMaximum(_maxInterFrameGapUs, gapUs);
        size_t omitted = 0;
        if (gapUs > 5'000) {
          omitted = std::max<size_t>(
              1, static_cast<size_t>((gapUs - 1'000 + 5'000) / 10'000));
          const auto expectedUs = 1'000 + omitted * 10'000;
          const auto residualUs = gapUs > expectedUs ? gapUs - expectedUs
                                                     : expectedUs - gapUs;
          // Infer omissions only for the distinctive gap pattern established
          // by the raw captures. An arbitrary delay must not shift all later
          // fields into plausible but incorrect offsets.
          if (residualUs > 3'000) {
            _invalidSnapshots.fetch_add(1);
            resetPayload();
            return;
          }
        }
        _logicalChunkIndex += 1 + omitted;
        _previousChunkTimestampUs = frame.timestampUs;
        if (_logicalChunkIndex >= RuntimeChunks) {
          _invalidSnapshots.fetch_add(1);
          resetPayload();
          return;
        }
        if (omitted)
          _inferredMissingChunks.fetch_add(omitted);
        storePayloadChunk(_logicalChunkIndex, frame.data, frame.dlc);
        if (_logicalChunkIndex == RuntimeChunks - 1 || frame.dlc == 4)
          finishPayload(now);
      }

      static void updateMaximum(std::atomic<uint64_t>& destination,
                                uint64_t value) {
        auto current = destination.load();
        while (current < value &&
               !destination.compare_exchange_weak(current, value)) {
        }
      }

      void resetAssembler() {
        resetPayload();
        _pendingCrc = 0;
        _pendingCrcPresent = false;
        _headerUs = 0;
      }

      void resetPayload() {
        _assembling = false;
        _payloadCrc = 0;
        _payloadCrcPresent = false;
        _payloadChunks = 0;
        _payloadLength = 0;
        _logicalChunkIndex = 0;
        _payloadChunkMask = 0;
        _previousChunkTimestampUs = 0;
        std::memset(_payload, 0, sizeof(_payload));
      }

      void storePayloadChunk(size_t index, const uint8_t* data, size_t size) {
        const auto offset = index * 8;
        const auto count = std::min(size, RuntimeLength - offset);
        if (count)
          std::memcpy(_payload + offset, data, count);
        _payloadLength += count;
        _payloadChunks++;
        _payloadChunkMask |= static_cast<uint32_t>(1) << index;
      }

      void finishPayload(int64_t now) {
        const bool complete = _payloadLength == RuntimeLength &&
                              _payloadChunkMask == RuntimeChunkMask;
        const auto calculated =
            complete ? stm32Crc32(_payload, RuntimeLength) : 0;
        const bool valid = complete && _payloadCrcPresent &&
                           calculated == _payloadCrc &&
                           !std::memcmp(_payload, RuntimeMagic,
                                        sizeof(RuntimeMagic));
        if (valid) {
          decodeRuntime(_payload, now);
          _validSnapshots.fetch_add(1);
          _lastValidRuntimeUs.store(now);
          publishState();
        } else {
          _invalidSnapshots.fetch_add(1);
          if (decodeRuntimePartial(now)) {
            _partialSnapshots.fetch_add(1);
            publishState();
          }
        }
        resetPayload();
      }

      bool hasPayloadChunk(size_t index) const {
        return _payloadChunkMask & (static_cast<uint32_t>(1) << index);
      }

      bool hasPayloadChunks(size_t first, size_t last) const {
        for (auto index = first; index <= last; index++)
          if (!hasPayloadChunk(index))
            return false;
        return true;
      }

      bool decodeRuntimePartial(int64_t now) {
        // State 0x52 is in logical chunk 10. Without it, a partial response
        // cannot safely advance the user-visible heater state.
        if (!hasPayloadChunk(10) || _payload[0x52] > 31 ||
            _payload[0x53] > 31)
          return false;

        Runtime runtime;
        {
          std::lock_guard guard(_mutex);
          runtime = _runtime;
        }
        runtime.valid = true;
        runtime.partial = true;
        runtime.chunkMask = _payloadChunkMask;
        if (hasPayloadChunk(0)) {
          std::memcpy(runtime.firmwareTag, _payload, 8);
          runtime.firmwareTag[8] = 0;
        }
        runtime.state = _payload[0x52];
        runtime.nextState = _payload[0x53];
        runtime.thermalSubstate = _payload[0x54];
        runtime.fanControlState = _payload[0x55];
        runtime.fanControlNextState = _payload[0x56];

        if (hasPayloadChunks(9, 10)) {
          std::memcpy(runtime.faultFlags, _payload + 0x48,
                      sizeof(runtime.faultFlags));
          runtime.faultCode = displayErrorCode(_payload);
          runtime.faultActive = runtime.faultCode != 0xFF;
        }
        if (hasPayloadChunks(3, 6)) {
          runtime.displayOperationMode = _payload[0x1C];
          runtime.displayControlMode = _payload[0x1D];
          runtime.displaySpecialTarget = readU32(_payload + 0x20);
          runtime.displayTemperatureC = readFloat(_payload + 0x24);
          runtime.displayPowerW = readFloat(_payload + 0x28);
          runtime.displayOnOff = _payload[0x35];
        }
        if (hasPayloadChunk(11)) {
          runtime.pumpFrequencyHz = readFloat(_payload + 0x58);
          runtime.pumpPulseCount = readU32(_payload + 0x5C);
        }
        if (hasPayloadChunk(12)) {
          runtime.glowResistanceOhm = readFloat(_payload + 0x60);
          runtime.glowVoltageV = readFloat(_payload + 0x64);
        }
        if (hasPayloadChunk(13)) {
          runtime.glowMinimumVoltageV = readFloat(_payload + 0x68);
          runtime.glowDuty = readFloat(_payload + 0x6C);
        }
        if (hasPayloadChunk(14)) {
          runtime.glowPowerW = readFloat(_payload + 0x70);
          runtime.fanRpm = readI32(_payload + 0x74);
        }
        if (hasPayloadChunk(15)) {
          runtime.fanDrive = readFloat(_payload + 0x78);
          runtime.fanTargetRpm = readI32(_payload + 0x7C);
        }
        if (hasPayloadChunk(16)) {
          runtime.sensorA_C = readFloat(_payload + 0x80);
          runtime.sensorB_C = readFloat(_payload + 0x84);
        }
        if (hasPayloadChunk(17)) {
          runtime.chamberC = readFloat(_payload + 0x88);
          runtime.ambientC = readFloat(_payload + 0x8C);
        }
        if (hasPayloadChunk(19))
          runtime.supplyVoltageV = readFloat(_payload + 0x98);
        runtime.receivedUs = now;

        std::lock_guard guard(_mutex);
        _runtime = runtime;
        return true;
      }

      void decodeRuntime(const uint8_t* payload, int64_t now) {
        Runtime runtime;
        runtime.valid = true;
        runtime.partial = false;
        runtime.chunkMask = RuntimeChunkMask;
        std::memcpy(runtime.firmwareTag, payload, 8);
        runtime.firmwareTag[8] = 0;
        runtime.state = payload[0x52];
        runtime.nextState = payload[0x53];
        runtime.thermalSubstate = payload[0x54];
        runtime.fanControlState = payload[0x55];
        runtime.fanControlNextState = payload[0x56];
        std::memcpy(runtime.faultFlags, payload + 0x48,
                    sizeof(runtime.faultFlags));
        runtime.faultCode = displayErrorCode(payload);
        runtime.faultActive = runtime.faultCode != 0xFF;

        runtime.displayOperationMode = payload[0x1C];
        runtime.displayControlMode = payload[0x1D];
        runtime.displaySpecialTarget = readU32(payload + 0x20);
        runtime.displayTemperatureC = readFloat(payload + 0x24);
        runtime.displayPowerW = readFloat(payload + 0x28);
        runtime.displayOnOff = payload[0x35];

        runtime.pumpFrequencyHz = readFloat(payload + 0x58);
        runtime.pumpPulseCount = readU32(payload + 0x5C);
        runtime.glowResistanceOhm = readFloat(payload + 0x60);
        runtime.glowVoltageV = readFloat(payload + 0x64);
        runtime.glowMinimumVoltageV = readFloat(payload + 0x68);
        runtime.glowDuty = readFloat(payload + 0x6C);
        runtime.glowPowerW = readFloat(payload + 0x70);
        runtime.fanRpm = readI32(payload + 0x74);
        runtime.fanDrive = readFloat(payload + 0x78);
        runtime.fanTargetRpm = readI32(payload + 0x7C);
        runtime.sensorA_C = readFloat(payload + 0x80);
        runtime.sensorB_C = readFloat(payload + 0x84);
        runtime.chamberC = readFloat(payload + 0x88);
        runtime.ambientC = readFloat(payload + 0x8C);
        runtime.supplyVoltageV = readFloat(payload + 0x98);
        runtime.receivedUs = now;

        {
          std::lock_guard guard(_mutex);
          _runtime = runtime;
        }
        observeThermostat(runtime);
      }

      void updateConnection() {
        const auto last = _lastApplicationResponseUs.load();
        const bool connected =
            last && esp_timer_get_time() - last <= ConnectionTimeoutUs;
        if (_connected.exchange(connected) != connected) {
          if (!connected) {
            std::lock_guard control(_controlMutex);
            manualTakeover();
          }
          publishState();
        }
      }

      void updateComponents() {
        // The heater device is deliberately constructed before App::Init so
        // TWAI can acknowledge the heater's cold-start traffic. Component
        // setters publish ComponentStateChanged events, however, and must not
        // run while app_main is still constructing/subscribing the remaining
        // esp32m objects. Keep the early protocol worker independent of the
        // framework event graph until initialization has completed.
        if (!App::initialized())
          return;

        const auto desiredState = desired();
        Runtime runtime;
        {
          std::lock_guard guard(_mutex);
          runtime = _runtime;
        }
        const bool runtimeStale =
            runtimeIsStale(runtime, esp_timer_get_time());
        const bool mode1Pumping =
            runtime.valid && runtime.state == 23 && _priming.load() &&
            _commandedOn.load();

        JsonDocument climateDoc;
        auto climate = climateDoc.to<JsonObject>();
        climate["mode"] = _commandedOn.load() ? "heat" : "off";
        climate["action"] = climateAction(runtime.state, runtime.valid);
        climate["temperature"] = desiredState.temperatureC;
        const bool partialHasFault =
            runtime.partial &&
            (runtime.chunkMask & ((1U << 9) | (1U << 10))) ==
                ((1U << 9) | (1U << 10));
        const bool partialHasAmbient =
            runtime.partial && (runtime.chunkMask & (1U << 17));
        if (runtime.valid && !runtimeStale &&
            (!runtime.partial || partialHasAmbient) &&
            std::isfinite(runtime.ambientC))
          climate["currentTemperature"] = runtime.ambientC;
        else
          climate["currentTemperature"] = nullptr;
        _climate.setState(climateDoc.as<JsonVariantConst>());
        _powerSetpoint.setState(desiredState.powerW);
        _controlMode.set(controlModeName(desiredState.controlMode));
        _connectedSensor.set(_connected.load());
        if (!runtime.partial || partialHasFault)
          _faultSensor.set(runtime.valid && runtime.faultActive);
        else
          _faultSensor.resetState();

        if (!runtime.valid || runtimeStale) {
          _stateSensor.resetState();
          _chamberSensor.resetState();
          _ambientSensor.resetState();
          _voltageSensor.resetState();
          _fanSensor.resetState();
          _pumpSensor.resetState();
          _glowPowerSensor.resetState();
          _faultCodeSensor.resetState();
          return;
        }
        _stateSensor.set(heaterStateName(runtime.state));
        // A partially recovered response advances the state machine in the UI,
        // but fields carried by missing chunks may still contain values from
        // the preceding complete response. Do not publish those mixed-age
        // measurements as current Home Assistant sensor readings.
        if (runtime.partial) {
          _chamberSensor.resetState();
          _ambientSensor.resetState();
          _voltageSensor.resetState();
          _fanSensor.resetState();
          _pumpSensor.resetState();
          _glowPowerSensor.resetState();
          _faultCodeSensor.resetState();
          return;
        }
        if (std::isfinite(runtime.chamberC))
          _chamberSensor.setSample(runtime.chamberC, uint64_t(runtime.receivedUs / 1000));
        else
          _chamberSensor.resetState();
        if (std::isfinite(runtime.ambientC))
          _ambientSensor.setSample(runtime.ambientC, uint64_t(runtime.receivedUs / 1000));
        else
          _ambientSensor.resetState();
        if (std::isfinite(runtime.supplyVoltageV))
          _voltageSensor.set(runtime.supplyVoltageV);
        else
          _voltageSensor.resetState();
        _fanSensor.set(runtime.fanRpm);
        if (!mode1Pumping && std::isfinite(runtime.pumpFrequencyHz))
          _pumpSensor.set(runtime.pumpFrequencyHz);
        else
          _pumpSensor.resetState();
        if (std::isfinite(runtime.glowPowerW))
          _glowPowerSensor.set(runtime.glowPowerW);
        else
          _glowPowerSensor.resetState();
        _faultCodeSensor.set(runtime.faultCode);
      }

      void publishState() {
        if (!App::initialized())
          return;
        updateComponents();
        EventStateChanged::publish(_owner);
      }

      void snapshotCanStatus() {
        const auto now = esp_timer_get_time();
        if (!_node || now - _lastStatusUs < 1'000'000)
          return;
        _lastStatusUs = now;
        twai_node_status_t status = {};
        twai_node_record_t record = {};
        if (twai_node_get_info(_node, &status, &record) == ESP_OK) {
          _canState.store(status.state);
          _txErrorCount.store(status.tx_error_count);
          _rxErrorCount.store(status.rx_error_count);
          _busErrors.store(record.bus_err_num);
        }
        Runtime runtime;
        {
          std::lock_guard guard(_mutex);
          runtime = _runtime;
        }
        if (_priming.load() &&
            runtimeIsStale(runtime, esp_timer_get_time()))
          publishState();
      }

      void recoverCan() {
        if (!_node || _canState.load() != TWAI_ERROR_BUS_OFF) {
          _recoveryRequested = false;
          return;
        }
        if (_recoveryRequested)
          return;
        const auto result = twai_node_recover(_node);
        if (result == ESP_OK)
          _recoveryRequested = true;
        else
          _lastError.store(result);
      }

      static bool onReceive(
          twai_node_handle_t node, const twai_rx_done_event_data_t*,
          void* context) {
        auto* self = static_cast<Impl*>(context);
        const auto hardware = readTwaiHardwareRxStatus();
        updateMaximum32(self->_maxHardwareRxPending, hardware.pending);
        RxFrame record;
        twai_frame_t frame = {};
        frame.buffer = record.data;
        frame.buffer_len = sizeof(record.data);
        if (twai_node_receive_from_isr(node, &frame) != ESP_OK)
          return false;
        record.id = frame.header.id;
        record.timestampUs = frame.header.timestamp;
        record.extended = frame.header.ide;
        record.remote = frame.header.rtr;
        record.dlc = static_cast<uint8_t>(
            std::min<size_t>(frame.header.dlc, sizeof(record.data)));
        BaseType_t taskWoken = pdFALSE;
        if (xQueueSendFromISR(self->_rxQueue, &record, &taskWoken) != pdTRUE)
          self->_rxDropped.fetch_add(1);
        return taskWoken == pdTRUE;
      }

      static bool onTransmitDone(
          twai_node_handle_t, const twai_tx_done_event_data_t* event,
          void* context) {
        auto* self = static_cast<Impl*>(context);
        if (event->done_tx_frame != &self->_txFrame)
          return false;
        self->_txCompletionSucceeded.store(event->is_tx_success);
        self->_txInFlight.store(false);
        BaseType_t taskWoken = pdFALSE;
        xSemaphoreGiveFromISR(self->_txDone, &taskWoken);
        return taskWoken == pdTRUE;
      }

      static bool onError(twai_node_handle_t,
                          const twai_error_event_data_t* event,
                          void* context) {
        auto* self = static_cast<Impl*>(context);
        const auto now = static_cast<uint64_t>(esp_timer_get_time());
        const auto flags = event ? event->err_flags.val : 0;
        const auto hardware = readTwaiHardwareRxStatus();
        self->_lastErrorHardwareStatus.store(hardware.status);
        self->_lastErrorRxPending.store(hardware.pending);
        updateMaximum32(self->_maxHardwareRxPending, hardware.pending);
#if CONFIG_IDF_TARGET_ESP32S3
        if (hardware.status & TWAI_LL_STATUS_MS)
          self->_hardwareRxMissEvents.fetch_add(1);
        if (hardware.status & TWAI_LL_STATUS_DOS)
          self->_hardwareDataOverrunEvents.fetch_add(1);
#endif
        const auto previous = self->_lastErrorEventUs.exchange(now);
        const bool newBurst = !previous || now < previous ||
                              now - previous > 100'000;
        self->_errorEvents.fetch_add(1);
        if (!flags)
          self->_unclassifiedErrorEvents.fetch_add(1);
        if (newBurst) {
          self->_errorBursts.fetch_add(1);
          self->_lastErrorBurstStartUs.store(now);
          self->_lastErrorBurstEvents.store(1);
          self->_lastErrorBurstFlags.store(flags);
          self->_lastErrorBurstHardwareStatus.store(hardware.status);
          self->_lastErrorBurstMaxRxPending.store(hardware.pending);
          self->_lastErrorBurstRequest.store(
              self->_runtimeRequests.load());
          const auto requestUs = self->_lastRuntimeRequestUs.load();
          self->_lastErrorBurstOffsetFromRequestUs.store(
              requestUs > 0 && now >= static_cast<uint64_t>(requestUs)
                  ? static_cast<int64_t>(now - requestUs)
                  : -1);
        } else {
          self->_lastErrorBurstEvents.fetch_add(1);
          self->_lastErrorBurstFlags.fetch_or(flags);
          self->_lastErrorBurstHardwareStatus.fetch_or(hardware.status);
          updateMaximum32(self->_lastErrorBurstMaxRxPending,
                          hardware.pending);
        }
        self->_lastErrorBurstEndUs.store(now);
        if (event) {
          if (event->err_flags.arb_lost)
            self->_arbitrationLostErrors.fetch_add(1);
          if (event->err_flags.bit_err)
            self->_bitErrors.fetch_add(1);
          if (event->err_flags.form_err)
            self->_formErrors.fetch_add(1);
          if (event->err_flags.stuff_err)
            self->_stuffErrors.fetch_add(1);
          if (event->err_flags.ack_err)
            self->_ackErrors.fetch_add(1);
        }
        self->_lastError.store(ESP_FAIL);
        return false;
      }

      static bool onStateChange(
          twai_node_handle_t, const twai_state_change_event_data_t* event,
          void* context) {
        static_cast<Impl*>(context)->_canState.store(event->new_sta);
        return false;
      }
    };

    Lfbros::Lfbros(const char* name, const Options& options)
        : _name(name ? name : "lfbros"),
          _impl(std::make_unique<Impl>(this, options)) {
      Device::init(Flags::None);
      ESP_ERROR_CHECK_WITHOUT_ABORT(_impl->start());
    }

    Lfbros::~Lfbros() = default;

    esp_err_t Lfbros::turn(bool on) {
      return _impl->turn(on);
    }

    esp_err_t Lfbros::clearFault() {
      return _impl->clearFault();
    }

    esp_err_t Lfbros::startPriming() {
      return _impl->startPriming();
    }

    esp_err_t Lfbros::setTemperature(float valueC) {
      return _impl->setTemperature(valueC);
    }

    esp_err_t Lfbros::setPower(float valueW) {
      return _impl->setPower(valueW);
    }

    esp_err_t Lfbros::setControlMode(ControlMode mode) {
      return _impl->setControlMode(mode);
    }

    float Lfbros::minimumPower() const {
      return _impl->minimumPower();
    }

    float Lfbros::maximumPower() const {
      return _impl->maximumPower();
    }
    thermostat::Capabilities Lfbros::capabilities() const { return _impl->thermostatCapabilities(); }
    thermostat::Feedback Lfbros::snapshot() const { return _impl->thermostatSnapshot(); }
    thermostat::Result Lfbros::apply(const thermostat::Request& request) { return _impl->thermostatApply(request); }
    void Lfbros::thermostatAttached(bool attached) { _impl->setThermostatAttached(attached); }

    const JsonVariantConst Lfbros::descriptor() const {
      static JsonDocument doc;
      if (doc.isNull()) {
        auto root = doc.to<JsonArray>();
        root.add("lf-bros");
      }
      return doc.as<JsonArrayConst>();
    }

    JsonDocument* Lfbros::getState(RequestContext&) {
      auto* doc = new JsonDocument();
      _impl->toState(doc->to<JsonObject>());
      return doc;
    }

    void Lfbros::setState(RequestContext& ctx) {
      const bool startRequested =
          ctx.data["on"].is<bool>() && ctx.data["on"].as<bool>();
      const bool primingRequested =
          ctx.data["polr"].is<bool>() && ctx.data["polr"].as<bool>();
      const bool clearFaultRequested =
          ctx.data["clearFault"].is<bool>() &&
          ctx.data["clearFault"].as<bool>();
      if (static_cast<unsigned>(startRequested) +
              static_cast<unsigned>(primingRequested) +
              static_cast<unsigned>(clearFaultRequested) >
          1) {
        ctx.errors.add(
            "start, Polr, and fault clear cannot be requested together");
        return;
      }

      if (ctx.data["on"].is<bool>())
        ctx.errors.check(turn(ctx.data["on"].as<bool>()));

      if (primingRequested)
        ctx.errors.check(startPriming());

      if (clearFaultRequested)
        ctx.errors.check(clearFault());

      if (ctx.data["temperatureSetpointC"].is<float>() ||
          ctx.data["temperatureSetpointC"].is<int>())
        ctx.errors.check(
            setTemperature(ctx.data["temperatureSetpointC"].as<float>()));

      if (ctx.data["powerSetpointW"].is<float>() ||
          ctx.data["powerSetpointW"].is<int>())
        ctx.errors.check(setPower(ctx.data["powerSetpointW"].as<float>()));

      auto modeString = ctx.data["controlMode"].as<const char*>();
      if (modeString) {
        ControlMode mode;
        if (parseControlMode(modeString, mode))
          ctx.errors.check(setControlMode(mode));
        else
          ctx.errors.add("invalid control mode");
      }
    }

    JsonDocument* Lfbros::getConfig(RequestContext&) {
      auto* doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      const auto desired = _impl->desired();
      root["controlMode"] = controlModeName(desired.controlMode);
      root["temperatureSetpointC"] = desired.temperatureC;
      root["powerSetpointW"] = desired.powerW;
      return doc;
    }

    bool Lfbros::setConfig(RequestContext& ctx) {
      auto desired = _impl->desired();
      bool changed = false;

      auto modeString = ctx.data["controlMode"].as<const char*>();
      if (modeString) {
        ControlMode mode;
        if (!parseControlMode(modeString, mode))
          ctx.errors.add("invalid control mode");
        else if (desired.controlMode != mode) {
          desired.controlMode = mode;
          changed = true;
        }
      }

      if (ctx.data["temperatureSetpointC"].is<float>() ||
          ctx.data["temperatureSetpointC"].is<int>()) {
        const auto value = ctx.data["temperatureSetpointC"].as<float>();
        if (!std::isfinite(value) || value < 10 || value > 35)
          ctx.errors.add("temperature setpoint must be between 10 and 35 C");
        else if (desired.temperatureC != value) {
          desired.temperatureC = value;
          changed = true;
        }
      }

      if (ctx.data["powerSetpointW"].is<float>() ||
          ctx.data["powerSetpointW"].is<int>()) {
        const auto value = ctx.data["powerSetpointW"].as<float>();
        if (!std::isfinite(value) || value < minimumPower() ||
            value > maximumPower())
          ctx.errors.add("power setpoint must be between %.0f and %.0f W",
                         minimumPower(), maximumPower());
        else if (desired.powerW != value) {
          desired.powerW = value;
          changed = true;
        }
      }

      if (changed)
        _impl->setDesired(desired);
      return changed;
    }

    bool Lfbros::handleRequest(Request& req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is(integrations::ha::DescribeRequest::Name)) {
        _impl->describeHa(req);
        return true;
      }
      if (req.is(integrations::ha::CommandRequest::Name)) {
        auto& command = static_cast<integrations::ha::CommandRequest&>(req);
        if (!_impl->handleHaCommand(command))
          logW("invalid HA command for %s: %s", command.componentId.c_str(),
               req.data().as<const char*>());
        req.respond();
        return true;
      }
      return false;
    }

    Lfbros* useLfbros(const Lfbros::Options& options, const char* name) {
      return new Lfbros(name, options);
    }

  }  // namespace dev
}  // namespace esp32m
