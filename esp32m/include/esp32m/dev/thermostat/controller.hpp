#pragma once

#include "esp32m/dev/thermostat/types.hpp"

namespace esp32m::dev::thermostat {

class Controller {
 public:
  Decision evaluate(const Config& config, const Input& input);
  void applied(const Request& request, Result result, uint64_t nowMs);
  // Explicit rearm/configuration change; does not replay previously sent pulses.
  void reset();

 private:
  bool _demand = false;
  bool _havePhase = false;
  Phase _phase = Phase::Unknown;
  uint64_t _phaseAt = 0;
  bool _running = false;
  uint64_t _runAt = 0;
  bool _low = false;
  uint64_t _lowAt = 0;
  bool _haveRevision = false;
  uint64_t _revision = 0;
  bool _manual = false;
  bool _fault = false;
  bool _uncertain = false;
  Command _pending = Command::None;
  uint64_t _pendingAt = 0;
  bool _released = false;
  bool _haveApplied = false;
  Request _lastApplied;
  uint64_t _lastAppliedAt = 0;
};

}  // namespace esp32m::dev::thermostat
