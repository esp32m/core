#include "esp32m/dev/thermostat/controller.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace esp32m::dev::thermostat;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ": " #x); } while (0)

struct Fixture {
  Controller controller;
  Config config;
  Input input;
  Fixture() {
    config.enabled = true;
    config.shadow = false;
    config.minimumOffMs = 100;
    config.minimumRunMs = 100;
    config.lowPowerHoldMs = 50;
    config.commandTimeoutMs = 1000;
    input.sourceIsNative = true;
    input.capabilities.nativeTemperature = true;
    input.capabilities.nativeTarget = true;
    input.capabilities.startStop = true;
    input.feedback.valid = true;
    input.feedback.phase = Phase::Off;
    input.temperature = {15, 1000, true};
    at(1000);
  }
  void at(uint64_t now) {
    input.nowMs = now;
    input.feedback.sampledAtMs = now;
    input.temperature.sampledAtMs = now;
  }
  Decision tick() { return controller.evaluate(config, input); }
  Decision apply() {
    auto d = tick();
    if (d.request.command != Command::None)
      controller.applied(d.request, Result::Accepted, input.nowMs);
    return d;
  }
  void flow() {
    input.sourceIsNative = false;
    input.capabilities = {};
    input.capabilities.flow = true;
    input.capabilities.deviceGate = true;
    input.capabilities.minimumFlowC = 20;
    input.capabilities.maximumFlowC = 80;
  }
};

void configuration() {
  Config c;
  CHECK(validConfig(c));
  c.targetC = NAN; CHECK(!validConfig(c));
  c = {}; c.minimumRunMs = UINT32_MAX; CHECK(!validConfig(c));
  c = {}; c.feedbackFreshnessMs = 0; CHECK(!validConfig(c));
  c = {}; c.highTemperatureC = c.targetC; CHECK(!validConfig(c));
}
void deviceOwnedGate() {
  Fixture f; f.flow();
  auto d = f.apply(); CHECK(d.request.command == Command::FlowTarget);
  CHECK(d.request.targetC >= f.config.minimumFlowC);
  f.input.feedback.enabled = false;
  f.input.temperature.valid = false;
  d = f.apply(); CHECK(d.request.command == Command::Release);
  CHECK(d.suspended && std::string(d.reason) == "device-disabled");
  d = f.apply(); CHECK(d.request.command == Command::None);
  f.input.feedback.enabled = true;
  f.input.temperature.valid = true;
  d = f.apply(); CHECK(d.request.command == Command::FlowTarget);
  // There is no CH-enable operation in the adapter contract or this path.
}
void flowRegulationAndZeroDemand() {
  Fixture f; f.flow();
  f.input.temperature.celsius = 14;
  auto d = f.apply(); CHECK(d.request.targetC == 65);
  f.at(3000); f.input.temperature.celsius = 17;
  d = f.apply(); CHECK(d.request.targetC > 25 && d.request.targetC < 65);
  f.at(5000); f.input.temperature.celsius = 20;
  d = f.apply(); CHECK(d.request.command == Command::FlowTarget && d.request.targetC == 0);
  f.at(7000); f.input.temperature.celsius = 18;
  d = f.apply(); CHECK(d.request.targetC == 0);
  f.at(9000); f.input.temperature.celsius = 16;
  d = f.apply(); CHECK(d.request.targetC > 0);
  f.config.heat = false;
  d = f.apply(); CHECK(d.request.targetC == 0);
}
void flowFreshnessAndLease() {
  Fixture f; f.flow();
  auto d = f.apply(); CHECK(d.request.validUntilMs == 6000);
  f.at(2000); d = f.apply(); CHECK(d.request.command == Command::None);
  f.at(3000); d = f.apply(); CHECK(d.request.command == Command::FlowTarget);
  CHECK(d.request.validUntilMs == 8000);
  f.input.temperature.sampledAtMs = 0;
  f.input.nowMs = 40000; f.input.feedback.sampledAtMs = 40000;
  d = f.apply(); CHECK(d.inhibited && d.request.targetC == 0);
  CHECK(std::string(d.reason) == "invalid-temperature");
}
void boundsAndUnsupported() {
  Fixture f; f.flow();
  f.input.capabilities.maximumFlowC = 50;
  CHECK(f.tick().request.targetC == 50);
  f.input.capabilities.maximumFlowC = 20;
  CHECK(std::string(f.tick().reason) == "flow-bounds");
  f.config.strategy = Strategy::Power;
  CHECK(std::string(f.tick().reason) == "unsupported-strategy");
  Fixture g; g.input.capabilities.nativeTemperature = false;
  CHECK(std::string(g.tick().reason) == "unqualified-native-sensor");
  g.input.sourceIsNative = false;
  CHECK(std::string(g.tick().reason) == "unsupported-strategy");
}
void minimumOffAndPrepareThenStart() {
  Fixture f;
  CHECK(std::string(f.apply().reason) == "minimum-off");
  f.at(1100);
  auto d = f.apply(); CHECK(d.request.command == Command::NativeTarget);
  f.at(1200);
  d = f.apply(); CHECK(d.request.command == Command::Start);
  f.at(1300);
  d = f.apply(); CHECK(d.request.command == Command::None);
  CHECK(std::string(d.reason) == "awaiting-command");
  f.input.feedback.phase = Phase::Starting;
  d = f.apply(); CHECK(std::string(d.reason) == "startup");
  f.at(3200); d = f.apply();
  CHECK(d.request.command == Command::NativeTarget);
}
void uncertainStartNeverRetries() {
  Fixture f;
  f.apply(); f.at(1100); f.apply(); f.at(1200); f.apply();
  f.at(2300);
  auto d = f.apply(); CHECK(std::string(d.reason) == "command-uncertain");
  f.at(30000); d = f.apply(); CHECK(d.request.command != Command::Start);
  CHECK(d.inhibited);
}
void cooldownIsObserved() {
  Fixture f;
  f.input.feedback.phase = Phase::Cooling;
  auto d = f.apply(); CHECK(std::string(d.reason) == "cooldown");
  f.at(100000); d = f.apply(); CHECK(d.request.command == Command::None);
  f.input.feedback.phase = Phase::Off;
  d = f.apply(); CHECK(std::string(d.reason) == "minimum-off");
  f.at(100100); CHECK(f.apply().request.command == Command::NativeTarget);
}
void lowOutputDwell() {
  Fixture f; f.input.feedback.phase = Phase::Heating;
  f.input.temperature.celsius = 20;
  f.input.feedback.minimumKnown = true;
  auto d = f.apply(); CHECK(d.request.command == Command::NativeTarget);
  f.at(1200); d = f.apply(); CHECK(d.request.command != Command::Stop);
  CHECK(std::string(d.reason) == "awaiting-low-output");
  f.input.feedback.atMinimum = true;
  f.at(1250); CHECK(f.apply().request.command != Command::Stop);
  f.at(1300); CHECK(f.apply().request.command == Command::Stop);
  f.at(1400); CHECK(f.apply().request.command == Command::None);
  f.input.feedback.phase = Phase::Cooling;
  CHECK(std::string(f.apply().reason) == "cooldown");
}
void nativeStandbyDefault() {
  Fixture f;
  f.input.capabilities.nativeStandby = true;
  f.input.feedback.phase = Phase::Standby;
  f.input.temperature.celsius = 21;
  f.apply(); f.at(50000);
  CHECK(f.apply().request.command != Command::Stop);
  f.config.allowFullShutdown = true;
  CHECK(f.apply().request.command == Command::Stop);
}
void externalStandbyGap() {
  Fixture f;
  f.input.sourceIsNative = false;
  f.input.capabilities = {};
  f.input.capabilities.power = true;
  f.input.capabilities.startStop = true;
  f.input.capabilities.externalRequiresShutdown = true;
  auto d = f.apply(); CHECK(std::string(d.reason) == "external-standby-unsupported");
  CHECK(d.request.command == Command::None);
  f.config.allowFullShutdown = true;
  d = f.apply(); CHECK(d.available);
  f.at(1100); CHECK(f.apply().request.command == Command::Power);
}
void ceilingAndExplicitOffBypassDwell() {
  Fixture f;
  f.input.feedback.phase = Phase::Heating;
  f.input.temperature.celsius = 36;
  auto d = f.apply(); CHECK(d.request.command == Command::Stop);
  CHECK(std::string(d.reason) == "temperature-ceiling");
  Fixture g;
  g.input.feedback.phase = Phase::Starting;
  g.config.heat = false;
  CHECK(g.apply().request.command == Command::Stop);
}
void invalidSensorStopsButStaleLinkDoesNotPretend() {
  Fixture f; f.input.feedback.phase = Phase::Heating;
  f.input.temperature.celsius = NAN;
  CHECK(f.apply().request.command == Command::Stop);
  Fixture g; g.input.feedback.valid = false;
  auto d = g.apply(); CHECK(d.inhibited && d.request.command == Command::None);
  CHECK(std::string(d.reason) == "stale-device");
}
void manualAndFaultLatches() {
  Fixture f; f.flow(); f.apply();
  f.input.feedback.revision++;
  auto d = f.apply(); CHECK(d.inhibited && d.request.command == Command::Release);
  f.config.targetC = 19;
  CHECK(std::string(f.tick().reason) == "manual-takeover");
  f.controller.reset(); CHECK(f.apply().request.command == Command::FlowTarget);
  f.input.feedback.fault = true;
  d = f.apply(); CHECK(std::string(d.reason) == "device-fault");
  CHECK(d.request.command == Command::Release);
  f.input.feedback.fault = false;
  f.at(6000);
  CHECK(std::string(f.tick().reason) == "device-fault");
  f.controller.reset();
  CHECK(f.tick().request.command == Command::FlowTarget);
}
void disabledAndRejected() {
  Fixture f; f.flow(); f.apply(); f.config.enabled = false;
  CHECK(f.apply().request.command == Command::Release);
  CHECK(f.apply().request.command == Command::None);
  Fixture g; g.flow();
  auto d = g.tick();
  g.controller.applied(d.request, Result::Rejected, g.input.nowMs);
  CHECK(std::string(g.tick().reason) == "manual-takeover");
}
void changedNativeTargetBeforeStart() {
  Fixture f;
  f.apply(); f.at(1100); f.apply();
  f.at(1200); f.config.targetC = 20;
  auto d = f.apply();
  CHECK(d.request.command == Command::NativeTarget && d.request.targetC == 20);
  f.at(1300); CHECK(f.apply().request.command == Command::Start);
}
void chGateRaceDoesNotLatchManual() {
  Fixture f; f.flow();
  auto d = f.tick();
  f.controller.applied(d.request, Result::Unavailable, f.input.nowMs);
  f.input.feedback.enabled = false;
  CHECK(std::string(f.tick().reason) == "device-disabled");
  f.input.feedback.enabled = true;
  CHECK(f.tick().request.command == Command::FlowTarget);
}
void standbyDoesNotRestartMinimumRun() {
  Fixture f;
  f.input.capabilities.nativeStandby = true;
  f.config.allowFullShutdown = true;
  f.input.feedback.phase = Phase::Heating;
  f.apply();
  f.at(5000);
  f.input.temperature.celsius = 21;
  f.input.feedback.phase = Phase::Standby;
  CHECK(f.apply().request.command == Command::Stop);
}
int main() {
  configuration(); deviceOwnedGate(); flowRegulationAndZeroDemand();
  flowFreshnessAndLease(); boundsAndUnsupported(); minimumOffAndPrepareThenStart();
  uncertainStartNeverRetries(); cooldownIsObserved(); lowOutputDwell();
  nativeStandbyDefault(); externalStandbyGap(); ceilingAndExplicitOffBypassDwell();
  invalidSensorStopsButStaleLinkDoesNotPretend(); manualAndFaultLatches(); disabledAndRejected();
  changedNativeTargetBeforeStart(); chGateRaceDoesNotLatchManual();
  standbyDoesNotRestartMinimumRun();
  std::cout << "thermostat controller: 18 groups passed\n";
}
