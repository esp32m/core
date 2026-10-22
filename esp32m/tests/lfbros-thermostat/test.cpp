#include "esp32m/dev/thermostat/types.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 2,
    ESP_ERR_INVALID_STATE = 3, ESP_ERR_TIMEOUT = 4;
using gpio_num_t = int;
struct JsonVariantConst {};
struct JsonDocument {};
struct RequestContext {};
namespace esp32m {
struct Request {};
struct Device {
  virtual ~Device() = default;
  virtual const char* name() const { return "fake"; }
  virtual const JsonVariantConst descriptor() const { return {}; }
  virtual JsonDocument* getState(RequestContext&) { return nullptr; }
  virtual void setState(RequestContext&) {}
  virtual JsonDocument* getConfig(RequestContext&) { return nullptr; }
  virtual bool setConfig(RequestContext&) { return false; }
  virtual bool handleRequest(Request&) { return false; }
};
namespace config { struct Changed { static inline unsigned count = 0; static void publish(Device*) { ++count; } }; }
}
#define private public
#define protected public
#include "header.inc"
#undef protected
#undef private

int64_t fakeNowUs = 1'000'000;
int64_t esp_timer_get_time() { return fakeNowUs; }
struct Queue { std::deque<std::vector<uint8_t>> items; size_t stride = 0; bool fail = false; };
using QueueHandle_t = Queue*;
constexpr int pdTRUE = 1;
int xQueueSend(QueueHandle_t queue, const void* item, int) {
  if (queue->fail) return 0;
  const auto* bytes = static_cast<const uint8_t*>(item);
  queue->items.emplace_back(bytes, bytes + queue->stride);
  return pdTRUE;
}
int xQueueReceive(QueueHandle_t queue, void* target, int) {
  if (queue->items.empty()) return 0;
  std::memcpy(target, queue->items.front().data(), queue->stride);
  queue->items.pop_front();
  return pdTRUE;
}
constexpr uint32_t ControlIdentifier = 0x201, ControllerResetIdentifier = 0x207;
constexpr int64_t ConnectionTimeoutUs = 5'000'000;

namespace esp32m::dev {
class Lfbros::Impl {
 public:
#include "declarations.inc"
  explicit Impl(const Options& options) : _options(options), _commandQueue(&_queue) {
    _queue.stride = sizeof(Command);
    _desired.powerW = options.initialPowerW;
    _desired.controlMode = options.initialControlMode;
    _runtime.valid = true;
    _runtime.state = 1;
    _runtime.faultCode = 0xff;
    _runtime.receivedUs = fakeNowUs;
    _runtime.ambientC = 20;
  }
  Options _options;
  Lfbros* _owner = nullptr;
  mutable std::mutex _mutex;
  mutable std::recursive_mutex _controlMutex;
  Desired _desired, _thermostatDesired;
  Runtime _runtime;
  uint64_t _manualRevision = 1, _thermostatLease = 1, _thermostatValidUntilMs = 0;
  int64_t _thermostatSentUs = 0, _normalModeRestoreRequestedUs = 0;
  bool _thermostatOverride = false, _thermostatPending = false, _thermostatUncertain = false;
  bool _thermostatRestorePending = false;
  std::atomic<bool> _thermostatAttached = false;
  struct Component { bool disabled = false; void setDisabled(bool value) { disabled = value; } };
  Component _climate, _powerSetpoint;
  CommandType _thermostatPulse = CommandType::TurnOff;
  std::atomic<bool> _connected = true, _sourceSelected = true, _priming = false,
      _normalModeRestorePending = false, _commandedOn = false;
  std::atomic<uint8_t> _operationMode = 0;
  std::atomic<int> _lastError = ESP_OK;
  std::atomic<int64_t> _lastApplicationResponseUs = fakeNowUs;
  Queue _queue;
  QueueHandle_t _commandQueue;
  struct Transmission { uint32_t id; std::vector<uint8_t> data; };
  std::vector<Transmission> transmissions;
  bool failTransmit = false;
  unsigned updates = 0;
  void updateComponents() { ++updates; }
  void publishState() { ++updates; }
  void resetSessionAfterControllerReset() { manualTakeover(); }
  esp_err_t sendFrame(uint32_t id, const uint8_t* bytes, size_t size) {
    transmissions.push_back({id, size ? std::vector<uint8_t>(bytes, bytes + size) : std::vector<uint8_t>()});
    return failTransmit ? ESP_FAIL : ESP_OK;
  }
#include "methods.inc"
};
// Platform-only construction avoids TWAI startup; interface overrides below
// are extracted verbatim from the production driver, never an adapter facade.
Lfbros::Lfbros(const char* name, const Options& options)
    : _name(name), _impl(std::make_unique<Impl>(options)) {}
Lfbros::~Lfbros() = default;
const JsonVariantConst Lfbros::descriptor() const { return {}; }
JsonDocument* Lfbros::getState(RequestContext&) { return nullptr; }
void Lfbros::setState(RequestContext&) {}
JsonDocument* Lfbros::getConfig(RequestContext&) { return nullptr; }
bool Lfbros::setConfig(RequestContext&) { return false; }
bool Lfbros::handleRequest(Request&) { return false; }
#include "forwarders.inc"
}

using Driver = esp32m::dev::Lfbros;
using namespace esp32m::dev::thermostat;
Request makeRequest(const Actuator& driver, Command command) {
  Request result;
  result.command = command;
  result.expectedRevision = driver.snapshot().revision;
  result.validUntilMs = fakeNowUs / 1000 + 5000;
  return result;
}
float sentFloat(const Driver::Impl& driver) {
  float result;
  assert(driver.transmissions.back().data.size() == 5);
  std::memcpy(&result, driver.transmissions.back().data.data() + 1, sizeof result);
  return result;
}
int main() {
  Driver::Options options(6, 7);
  Driver device("LF interface", options);
  auto& driver = *device._impl;
  Actuator& actuator = device;
  int primaryOwner = 0, otherOwner = 0;
  assert(!driver._thermostatAttached && !driver._climate.disabled && !driver._powerSetpoint.disabled);
  assert(actuator.attachThermostat(&primaryOwner));
  assert(driver._thermostatAttached && driver._climate.disabled && driver._powerSetpoint.disabled);
  assert(!actuator.attachThermostat(&otherOwner));
  actuator.detachThermostat(&otherOwner);
  assert(driver._thermostatAttached && driver._climate.disabled && driver._powerSetpoint.disabled);
  actuator.detachThermostat(&primaryOwner);
  assert(!driver._thermostatAttached && !driver._climate.disabled && !driver._powerSetpoint.disabled);
  assert(driver.transmissions.empty());
  auto cap = actuator.capabilities();
  assert(cap.nativeStandby && cap.externalRequiresShutdown && !cap.externalAmbient && !cap.nativeTemperature);
  assert(!actuator.snapshot().nativeTemperature.valid);
  auto power = makeRequest(actuator, Command::Power);
  power.powerFraction = 0.5f;
  assert(actuator.apply(power) == Result::Accepted);
  assert(esp32m::config::Changed::count == 0 && driver.desired().powerW == 2000);
  driver.processCommands();
  assert(driver.transmissions.back().data[0] == 1 && sentFloat(driver) == 3200);
  driver._runtime.state = 13;
  driver._runtime.pumpFrequencyHz = 1.72f;
  assert(!actuator.snapshot().minimumKnown);
  driver._options.thermostatLowPumpHz = 1.73f;
  assert(actuator.snapshot().atMinimum);
  driver._runtime.state = 16;
  driver._options.thermostatLowPumpHz = NAN;
  assert(actuator.snapshot().phase == Phase::Standby && actuator.snapshot().atMinimum);
  auto ambient = makeRequest(actuator, Command::NativeTarget);
  ambient.targetC = 18;
  ambient.ambientC = 20;
  assert(actuator.apply(ambient) == Result::Unsupported);
  driver._runtime.state = 1;
  auto start = makeRequest(actuator, Command::Start);
  assert(actuator.apply(start) == Result::Accepted);
  auto release = makeRequest(actuator, Command::Release);
  assert(actuator.apply(release) == Result::Accepted);
  const auto count = driver.transmissions.size();
  driver.processCommands();
  for (size_t i = count; i < driver.transmissions.size(); ++i)
    assert(driver.transmissions[i].data != std::vector<uint8_t>({3, 1}));
  assert(actuator.apply(start) == Result::Accepted);
  const auto oldEpoch = start.expectedRevision;
  assert(driver.setPower(2100) == ESP_OK);
  assert(actuator.snapshot().revision != oldEpoch);
  assert(actuator.apply(start) == Result::Rejected);
  const auto beforeManual = driver.transmissions.size();
  driver.processCommands();
  for (size_t i = beforeManual; i < driver.transmissions.size(); ++i)
    assert(driver.transmissions[i].data != std::vector<uint8_t>({3, 1}));
  power = makeRequest(actuator, Command::Power);
  power.powerFraction = 0;
  assert(actuator.apply(power) == Result::Accepted);
  driver.processCommands();
  fakeNowUs += 5'001'000;
  driver._runtime.receivedUs = fakeNowUs;
  driver.expireThermostat();
  driver.processCommands();
  assert(!driver._thermostatOverride && sentFloat(driver) == 2100);
  start = makeRequest(actuator, Command::Start);
  assert(actuator.apply(start) == Result::Accepted);
  driver.processCommands();
  assert(actuator.snapshot().commandPending);
  driver._runtime.state = 2;
  driver._runtime.receivedUs = ++fakeNowUs;
  driver.observeThermostat(driver._runtime);
  assert(!actuator.snapshot().commandPending);
  driver._runtime.partial = true;
  assert(!actuator.snapshot().valid);
  assert(actuator.apply(makeRequest(actuator, Command::Stop)) == Result::Unavailable);
  driver._runtime.partial = false;
  driver._runtime.faultActive = true;
  assert(actuator.apply(makeRequest(actuator, Command::Start)) == Result::Rejected);
  driver._runtime.faultActive = false;
  const auto epoch = actuator.snapshot().revision;
  driver._lastApplicationResponseUs = fakeNowUs - 6'000'000;
  driver.updateConnection();
  assert(!actuator.snapshot().valid && actuator.snapshot().revision != epoch);
  Driver delayedDevice("LF delayed", options);
  auto& delayed = *delayedDevice._impl;
  Actuator& delayedActuator = delayedDevice;
  auto expires = makeRequest(delayedActuator, Command::Start);
  expires.validUntilMs = fakeNowUs / 1000 + 5;
  assert(delayedActuator.apply(expires) == Result::Accepted);
  fakeNowUs += 6000;
  delayed.processCommands();
  assert(delayed.transmissions.empty() && !delayedActuator.snapshot().commandPending);
  auto queued = makeRequest(delayedActuator, Command::Start);
  assert(delayedActuator.apply(queued) == Result::Accepted);
  delayed._runtime.faultActive = true;
  delayed.processCommands();
  assert(delayed.transmissions.empty());
  delayed._runtime.faultActive = false;
  auto failed = makeRequest(delayedActuator, Command::Start);
  assert(delayedActuator.apply(failed) == Result::Accepted);
  delayed.failTransmit = true;
  delayed.processCommands();
  assert(delayedActuator.snapshot().commandUncertain);
  assert(delayedActuator.apply(failed) == Result::Rejected);
  Driver::Impl legacy(options);
  assert(legacy.turn(true) == ESP_OK);
  legacy.processCommands();
  assert(legacy.transmissions.size() == 3 && legacy.transmissions.back().data == std::vector<uint8_t>({3, 1}));
  std::cout << "LF Bros thermostat: actual production seams, native standby capability, qualified sensor/minimum, transient leases, manual cancellation, observed commands and stale/fault guards passed\n";
}
