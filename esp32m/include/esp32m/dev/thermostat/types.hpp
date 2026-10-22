#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>

namespace esp32m::dev::thermostat {

// No ESP-IDF, Device or transport dependencies: usable with a deterministic clock.
enum class Phase { Unknown, Off, Starting, Heating, Standby, Cooling, Fault };
enum class Strategy { Automatic, Native, Power, Flow };
enum class Command { None, NativeTarget, Power, FlowTarget, Start, Stop, Release };
enum class Result { Accepted, Busy, Unsupported, Unavailable, Invalid, Rejected };

struct Capabilities {
  bool startStop = false;
  bool nativeTarget = false;
  bool nativeTemperature = false;
  bool externalAmbient = false;
  bool power = false;
  bool flow = false;
  bool nativeStandby = false;
  // E.g. LF Bros cannot hold thermostat standby using external power control.
  bool externalRequiresShutdown = false;
  // The device owns this gate (OpenTherm CH). The thermostat NEVER changes it.
  bool deviceGate = false;
  float minimumFlowC = 20;
  float maximumFlowC = 80;
};

struct Temperature {
  float celsius = NAN;
  uint64_t sampledAtMs = 0;
  bool valid = false;
};

struct Feedback {
  uint64_t sampledAtMs = 0;
  bool valid = false;
  bool enabled = true;
  bool fault = false;
  bool manualOverride = false;
  bool commandPending = false;
  bool commandUncertain = false;
  uint64_t revision = 0;
  Phase phase = Phase::Unknown;
  bool minimumKnown = false;
  bool atMinimum = false;
  Temperature nativeTemperature;
};

struct Request {
  Command command = Command::None;
  uint64_t expectedRevision = 0;
  // Non-pulse requests are refreshed every two seconds by the controller.
  uint64_t validUntilMs = 0;
  float targetC = NAN;
  // Power 0 means the validated minimum stable output, 1 the validated maximum.
  // Stop is a distinct command. This is not a measured wattage or modulation cap.
  float powerFraction = NAN;
  float ambientC = NAN;
};

class Actuator {
 public:
  virtual ~Actuator() = default;
  virtual Capabilities capabilities() const = 0;
  virtual Feedback snapshot() const = 0;
  virtual Result apply(const Request& request) = 0;

  // Interface support is independent of attachment. A single controller may
  // claim this endpoint; a failed second claim must not release the first one.
  bool attachThermostat(const void* owner) {
    if (!owner) return false;
    std::lock_guard guard(_attachmentMutex);
    if (_attachmentOwner) return _attachmentOwner == owner;
    _attachmentOwner = owner;
    thermostatAttached(true);
    return true;
  }
  void detachThermostat(const void* owner) {
    std::lock_guard guard(_attachmentMutex);
    if (!owner || _attachmentOwner != owner) return;
    _attachmentOwner = nullptr;
    thermostatAttached(false);
  }

 protected:
  // Called under the attachment lock; implementations must not reenter claim
  // methods. This is local enrollment, never a heater start/stop operation.
  virtual void thermostatAttached(bool) {}

 private:
  std::mutex _attachmentMutex;
  const void* _attachmentOwner = nullptr;
};

struct Config {
  bool enabled = false;
  bool heat = true;
  bool shadow = true;
  bool allowFullShutdown = false;
  std::string sensorUid;  // Empty selects the qualified native room source.
  Strategy strategy = Strategy::Automatic;
  float targetC = 18;
  float heatHysteresisC = 1;
  float stopHysteresisC = 1.5f;
  float proportionalBandC = 3;
  float highTemperatureC = 35;
  float minimumFlowC = 25;
  float maximumFlowC = 65;
  uint32_t minimumRunMs = 600000;
  uint32_t minimumOffMs = 300000;
  uint32_t lowPowerHoldMs = 60000;
  uint32_t sensorFreshnessMs = 30000;
  uint32_t feedbackFreshnessMs = 5000;
  uint32_t commandTimeoutMs = 30000;
};

struct Input {
  uint64_t nowMs = 0;
  Temperature temperature;
  bool sourceIsNative = false;
  Capabilities capabilities;
  Feedback feedback;
};

struct Decision {
  Request request;
  bool available = false;
  bool heatingDemand = false;
  bool suspended = false;
  bool inhibited = false;
  Strategy strategy = Strategy::Automatic;
  const char* reason = "disabled";
};

const char* phaseName(Phase phase);
const char* strategyName(Strategy strategy);
const char* commandName(Command command);
bool validConfig(const Config& config);

}  // namespace esp32m::dev::thermostat
