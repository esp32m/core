#include "esp32m/dev/thermostat/controller.hpp"

#include <algorithm>
#include <limits>

namespace esp32m::dev::thermostat {
namespace {
bool fresh(uint64_t now, uint64_t stamp, uint32_t limit) {
  return stamp <= now && now - stamp <= limit;
}
bool sameFloat(float a, float b) {
  return (std::isnan(a) && std::isnan(b)) || std::fabs(a - b) < 0.01f;
}
bool sameRequest(const Request& a, const Request& b) {
  return a.command == b.command && sameFloat(a.targetC, b.targetC) &&
         sameFloat(a.powerFraction, b.powerFraction) &&
         sameFloat(a.ambientC, b.ambientC);
}
bool finiteRange(float v, float lo, float hi) {
  return std::isfinite(v) && v >= lo && v <= hi;
}
}  // namespace

const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Off: return "off";
    case Phase::Starting: return "starting";
    case Phase::Heating: return "heating";
    case Phase::Standby: return "standby";
    case Phase::Cooling: return "cooling";
    case Phase::Fault: return "fault";
    default: return "unknown";
  }
}
const char* strategyName(Strategy s) {
  switch (s) {
    case Strategy::Native: return "native";
    case Strategy::Power: return "power";
    case Strategy::Flow: return "flow";
    default: return "automatic";
  }
}
const char* commandName(Command c) {
  switch (c) {
    case Command::NativeTarget: return "native-target";
    case Command::Power: return "power";
    case Command::FlowTarget: return "flow-target";
    case Command::Start: return "start";
    case Command::Stop: return "stop";
    case Command::Release: return "release";
    default: return "none";
  }
}
bool validConfig(const Config& c) {
  constexpr uint32_t MaxDwell = 24 * 60 * 60 * 1000;
  return finiteRange(c.targetC, -20, 40) &&
         finiteRange(c.heatHysteresisC, 0.1f, 15) &&
         finiteRange(c.stopHysteresisC, 0.1f, 15) &&
         finiteRange(c.proportionalBandC, 0.1f, 30) &&
         finiteRange(c.highTemperatureC, c.targetC + 0.1f, 60) &&
         finiteRange(c.minimumFlowC, 0, 100) &&
         finiteRange(c.maximumFlowC, c.minimumFlowC, 100) &&
         c.minimumRunMs <= MaxDwell && c.minimumOffMs <= MaxDwell &&
         c.lowPowerHoldMs <= MaxDwell && c.sensorFreshnessMs >= 1000 &&
         c.sensorFreshnessMs <= MaxDwell && c.feedbackFreshnessMs >= 1000 &&
         c.feedbackFreshnessMs <= MaxDwell && c.commandTimeoutMs >= 1000 &&
         c.commandTimeoutMs <= MaxDwell &&
         static_cast<unsigned>(c.strategy) <= static_cast<unsigned>(Strategy::Flow);
}

void Controller::reset() { *this = Controller{}; }

Decision Controller::evaluate(const Config& c, const Input& in) {
  Decision d;
  const auto& f = in.feedback;
  const auto& cap = in.capabilities;
  const auto now = in.nowMs;
  auto issue = [&](Command command, float target = NAN, float fraction = NAN,
                   float ambient = NAN) {
    Request r;
    r.command = command;
    r.expectedRevision = f.revision;
    r.validUntilMs = now + 5000;
    r.targetC = target;
    r.powerFraction = fraction;
    r.ambientC = ambient;
    if (command == Command::Release) {
      if (_released || !_haveApplied) return;
    } else if (command != Command::Start && command != Command::Stop &&
               _haveApplied && sameRequest(r, _lastApplied) &&
               now >= _lastAppliedAt && now - _lastAppliedAt < 2000)
      return;
    d.request = r;
  };
  auto inhibit = [&](const char* reason, bool release = true) {
    d.inhibited = true;
    d.reason = reason;
    if (release) issue(Command::Release);
    return d;
  };
  if (!validConfig(c)) return inhibit("invalid-configuration");
  if (!c.enabled) {
    _demand = false;
    issue(Command::Release);
    return d;
  }
  // Device ownership is stronger than thermostat intent. Never emit a pulse or
  // target when OpenTherm's CH flag is disabled, even if room feedback is stale.
  if (cap.deviceGate && !f.enabled) {
    _demand = false;
    d.suspended = true;
    d.reason = "device-disabled";
    issue(Command::Release);
    return d;
  }
  if (!f.valid || !fresh(now, f.sampledAtMs, c.feedbackFreshnessMs)) {
    _low = false;
    return inhibit("stale-device");
  }

  if (!_haveRevision) {
    _revision = f.revision;
    _haveRevision = true;
  } else if (_revision != f.revision) {
    _revision = f.revision;
    _manual = true;
  }
  if (f.manualOverride) _manual = true;
  if (_manual) return inhibit("manual-takeover");

  if (!_havePhase || _phase != f.phase) {
    _phase = f.phase;
    _phaseAt = now;
    _havePhase = true;
  }
  if (f.phase == Phase::Heating || f.phase == Phase::Standby) {
    if (!_running) { _running = true; _runAt = now; }
  } else if (f.phase == Phase::Off || f.phase == Phase::Cooling) _running = false;
  if (f.minimumKnown && f.atMinimum && f.phase == Phase::Heating) {
    if (!_low) { _low = true; _lowAt = now; }
  } else _low = false;

  if ((_pending == Command::Start &&
       (f.phase == Phase::Starting || f.phase == Phase::Heating ||
        f.phase == Phase::Standby)) ||
      (_pending == Command::Stop &&
       (f.phase == Phase::Cooling || f.phase == Phase::Off)))
    _pending = Command::None;
  if (f.commandUncertain || (_pending != Command::None &&
      (now < _pendingAt || now - _pendingAt >= c.commandTimeoutMs)))
    _uncertain = true;
  if (_uncertain) return inhibit("command-uncertain");
  if (f.fault || f.phase == Phase::Fault) _fault = true;
  if (_fault) return inhibit("device-fault");

  Strategy s = c.strategy;
  if (s == Strategy::Automatic) {
    if (cap.flow) s = Strategy::Flow;
    else if (cap.nativeTarget && (in.sourceIsNative || cap.externalAmbient))
      s = Strategy::Native;
    else if (cap.power) s = Strategy::Power;
  }
  d.strategy = s;
  if ((s == Strategy::Flow && !cap.flow) ||
      (s == Strategy::Native && (!cap.nativeTarget ||
       (!in.sourceIsNative && !cap.externalAmbient))) ||
      (s == Strategy::Power && !cap.power) || s == Strategy::Automatic)
    return inhibit("unsupported-strategy");
  if (in.sourceIsNative && !cap.nativeTemperature)
    return inhibit("unqualified-native-sensor");
  if (s == Strategy::Power && cap.externalRequiresShutdown &&
      !c.allowFullShutdown)
    return inhibit("external-standby-unsupported");
  const float flowMin = std::max(c.minimumFlowC, cap.minimumFlowC);
  const float flowMax = std::min(c.maximumFlowC, cap.maximumFlowC);
  if (s == Strategy::Flow &&
      (!std::isfinite(cap.minimumFlowC) || !std::isfinite(cap.maximumFlowC) ||
       flowMin > flowMax))
    return inhibit("flow-bounds");

  const bool validTemperature = in.temperature.valid &&
      finiteRange(in.temperature.celsius, -100, 150) &&
      fresh(now, in.temperature.sampledAtMs, c.sensorFreshnessMs);
  if (!validTemperature) {
    d.inhibited = true;
    d.reason = "invalid-temperature";
    _demand = false;
    if (s == Strategy::Flow) issue(Command::FlowTarget, 0);
    else if (cap.startStop && !f.commandPending && _pending == Command::None &&
             (f.phase == Phase::Heating || f.phase == Phase::Starting ||
              f.phase == Phase::Standby)) issue(Command::Stop);
    else issue(Command::Release);
    return d;
  }
  d.available = true;
  const float t = in.temperature.celsius;
  const bool ceiling = t >= c.highTemperatureC;
  if (!c.heat || ceiling || t >= c.targetC + c.stopHysteresisC)
    _demand = false;
  else if (t <= c.targetC - c.heatHysteresisC) _demand = true;
  d.heatingDemand = _demand;
  const float fraction = std::clamp((c.targetC - t) / c.proportionalBandC, 0.f, 1.f);

  if (s == Strategy::Flow) {
    d.reason = !c.heat ? "off" : ceiling ? "temperature-ceiling" :
               _demand ? "regulating" : "satisfied";
    issue(Command::FlowTarget, _demand ?
          flowMin + fraction * (flowMax - flowMin) : 0.f);
    return d;
  }
  if (f.commandPending || _pending != Command::None) {
    d.reason = "awaiting-command";
    return d;
  }
  if (f.phase == Phase::Cooling) {
    d.reason = "cooldown";
    return d;
  }
  if (f.phase == Phase::Unknown) return inhibit("unknown-phase");
  if (f.phase == Phase::Off) {
    if (!_demand) { d.reason = c.heat ? "satisfied" : "off"; return d; }
    if (now - _phaseAt < c.minimumOffMs) {
      d.reason = "minimum-off";
      return d;
    }
    if (!cap.startStop) return inhibit("start-unsupported");
    d.reason = "starting";
    // Prepare the native target before the subsequent Start tick. This also
    // renews the actuator lease while observing a long minimum-off interval.
    Request target;
    target.command = s == Strategy::Native ? Command::NativeTarget : Command::Power;
    target.targetC = s == Strategy::Native ? c.targetC : NAN;
    target.ambientC = !in.sourceIsNative ? t : NAN;
    target.powerFraction = s == Strategy::Power ? fraction : NAN;
    if (!_haveApplied || target.command != _lastApplied.command ||
        (s == Strategy::Native && !sameFloat(target.targetC, _lastApplied.targetC)) ||
        _lastApplied.expectedRevision != f.revision ||
        now < _lastAppliedAt || now - _lastAppliedAt >= 5000) {
      issue(target.command, target.targetC, target.powerFraction, target.ambientC);
      return d;
    }
    issue(Command::Start);
    return d;
  }

  const bool canShutdown = !cap.nativeStandby || c.allowFullShutdown;
  if ((!c.heat || ceiling || (!_demand && canShutdown)) && cap.startStop) {
    const bool immediate = !c.heat || ceiling;
    const bool runningLongEnough = _running && now >= _runAt && now - _runAt >= c.minimumRunMs;
    const bool settledLow = f.phase == Phase::Standby ||
        (_low && now >= _lowAt && now - _lowAt >= c.lowPowerHoldMs);
    if (immediate || (runningLongEnough && settledLow)) {
      d.reason = ceiling ? "temperature-ceiling" : c.heat ? "satisfied-stop" : "off";
      issue(Command::Stop);
      return d;
    }
    d.reason = !runningLongEnough ? "minimum-run" : "awaiting-low-output";
  } else d.reason = _demand ? "regulating" : "native-standby";

  if (f.phase == Phase::Starting) {
    d.reason = "startup";
    if (s == Strategy::Native)
      issue(Command::NativeTarget, c.targetC, NAN, in.sourceIsNative ? NAN : t);
    else issue(Command::Power, NAN, _demand ? fraction : 0.f);
    return d;
  }
  // Renew the control lease while the native combustion loop runs. Native
  // standby remains native; the thermostat does not manufacture a standby CAN op.
  if (s == Strategy::Native)
    issue(Command::NativeTarget, c.targetC, NAN, in.sourceIsNative ? NAN : t);
  else issue(Command::Power, NAN, _demand ? fraction : 0.f);
  return d;
}

void Controller::applied(const Request& r, Result result, uint64_t now) {
  if (result != Result::Accepted) {
    if ((r.command == Command::Start || r.command == Command::Stop) &&
        result != Result::Busy) _uncertain = true;
    if (result == Result::Rejected) _manual = true;
    return;
  }
  if (r.command == Command::Start || r.command == Command::Stop) {
    _pending = r.command;
    _pendingAt = now;
  } else {
    _lastApplied = r;
    _lastAppliedAt = now;
    _released = r.command == Command::Release;
    _haveApplied = !_released;
  }
}

}  // namespace esp32m::dev::thermostat
