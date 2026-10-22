#include <ArduinoJson.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "esp32m/dev/thermostat/types.hpp"

using std::isnan;

using TaskHandle_t = void*;
using gpio_num_t = int;
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_ERR_INVALID_ARG = 1,
                    ESP_ERR_INVALID_STATE = 2;
static std::atomic<int64_t> nowMs{100};
int64_t esp_timer_get_time() { return nowMs.load() * 1000; }
template <typename... T> void xTaskCreate(T...) {}
template <typename T> const char* makeTaskName(T) { return "test"; }
#define ESP_CHECK_RETURN(call) do { auto error = (call); if (error) return error; } while (false)
#define logD(...) ((void)0)
namespace esp32m {
class INamed { public: virtual ~INamed() = default; virtual const char* name() const = 0; };
namespace log { class Loggable : public INamed {}; }
}
#include "production.inc"

namespace esp32m::dev {
// Device/framework glue is not part of these transport/control tests.
class OpenthermMaster : public opentherm::Master, public thermostat::Actuator {
 public:
  thermostat::Capabilities capabilities() const override;
  thermostat::Feedback snapshot() const override;
  thermostat::Result apply(const thermostat::Request&) override;
};
}
#include "actuator.inc"

using namespace esp32m;
using namespace esp32m::opentherm;

class Transport : public IDriver {
 public:
  uint32_t frame = 0;
  std::mutex mutex;
  std::condition_variable condition;
  bool block = false, entered = false, released = false;
  esp_err_t send(uint32_t value) override {
    std::unique_lock lock(mutex);
    frame = value;
    entered = true;
    condition.notify_all();
    if (block) condition.wait(lock, [this] { return released; });
    return ESP_OK;
  }
  esp_err_t receive(Recv&) override { return ESP_OK; }
};

class Device : public dev::OpenthermMaster {
 public:
  Transport transport;
  Device() {
    assert(init(&transport, new GenericMaster(), "boiler") == ESP_OK);
    hvac().status.master.value = 0x1b;  // CH/DHW/OTC/CH2 device-owned flags.
    hvac().tSet = 45;
    hvac().maxTSet = 65;
    hvac().tDhwSet = 52;
  }
  void queue(MessageType type, DataId id, uint16_t value, bool model = true) {
    assert(send(type, id, value, model) == ESP_OK);
  }
  void transmitQueued(bool retry = false) { assert(transmit(retry) == ESP_OK); }
  Message sent() { return Message(transport.frame); }
  void setGate(bool enabled) {
    std::lock_guard lock(_stateMutex);
    const auto oldFlow = hvac().tSet, oldMaximum = hvac().maxTSet;
    hvac().status.master.CH = enabled;
    reconcileManualFlow(oldFlow, oldMaximum);
  }
  void setManualFlow(float flow) {
    std::lock_guard lock(_stateMutex);
    const auto oldFlow = hvac().tSet, oldMaximum = hvac().maxTSet;
    hvac().tSet = flow;
    reconcileManualFlow(oldFlow, oldMaximum);
  }
  void status(uint8_t bits) {
    Message response(MessageType::ReadAck, DataId::Status, bits);
    processResponse(response);
  }
};

int main() {
  Device device;
  const auto flags = device.hvac().status.master.value;
  dev::thermostat::Actuator* actuator = &device;
  using namespace dev::thermostat;
  assert(!actuator->capabilities().startStop && actuator->capabilities().flow);
  assert(actuator->capabilities().deviceGate);
  assert(!actuator->snapshot().valid);
  Request flow;
  flow.command = Command::FlowTarget;
  flow.targetC = 60;
  flow.validUntilMs = 5000;
  assert(actuator->apply(flow) == Result::Accepted);
  device.queue(MessageType::WriteData, DataId::TSet, toF88(45));
  device.transmitQueued();
  assert(fromF88(device.sent().value) == 60);
  assert(device.hvac().tSet == 45 && device.hvac().tDhwSet == 52);
  assert(device.hvac().status.master.value == flags);
  for (auto command : {Command::Start, Command::Stop, Command::Power,
                       Command::NativeTarget}) {
    Request forbidden;
    forbidden.command = command;
    assert(actuator->apply(forbidden) == Result::Unsupported);
    assert(device.hvac().status.master.value == flags);
  }
  for (float value : {-1.0f, 66.0f, NAN}) {
    auto invalid = flow;
    invalid.targetC = value;
    assert(actuator->apply(invalid) == Result::Invalid);
    assert(device.heatingState().requestedFlowC == 60);
  }

  // No other message or raw diagnostic command may receive a TSet overlay.
  for (auto id : {DataId::TdhwSet, DataId::TsetCH2, DataId::MaxTSet}) {
    Message original(MessageType::WriteData, id, toF88(52));
    device.queue(original.type, original.id, original.value);
    device.transmitQueued();
    assert(device.transport.frame == original.frame);
  }
  device.queue(MessageType::WriteData, DataId::TSet, toF88(40), false);
  device.transmitQueued();
  assert(fromF88(device.sent().value) == 40);

  // CH may change between the controller snapshot, apply and actual transmit.
  device.queue(MessageType::WriteData, DataId::TSet, toF88(45));
  device.setGate(false);
  device.transmitQueued();
  assert(fromF88(device.sent().value) == 45);
  assert(!device.heatingState().overrideActive);
  assert(device.heatingState().revision == 0);
  assert(actuator->apply(flow) == Result::Unavailable);
  device.queue(MessageType::ReadData, DataId::Status, 0x1bca);
  device.transmitQueued();
  assert(device.sent().value == 0x1aca);  // CH cleared; all other bits preserved.
  device.setGate(true);
  assert(actuator->apply(flow) == Result::Accepted);

  // Retry preparation must reconcile the current gate, not replay the overlay.
  device.queue(MessageType::WriteData, DataId::TSet, toF88(45));
  device.transmitQueued();
  assert(fromF88(device.sent().value) == 60);
  device.setGate(false);
  device.transmitQueued(true);
  assert(fromF88(device.sent().value) == 45);
  device.setGate(true);

  // Flow zero is supported without changing CH; expired leases restore manual
  // configuration and do not fabricate a shutdown or write another setting.
  flow.targetC = 0;
  assert(actuator->apply(flow) == Result::Accepted);
  device.queue(MessageType::WriteData, DataId::TSet, toF88(45));
  device.transmitQueued();
  assert(device.sent().value == 0);
  assert(device.hvac().status.master.value == flags);
  nowMs = 5000;
  device.queue(MessageType::WriteData, DataId::TSet, toF88(45));
  device.transmitQueued();
  assert(fromF88(device.sent().value) == 45);
  assert(actuator->apply(flow) == Result::Invalid);

  nowMs = 6000;
  flow.targetC = 60;
  flow.validUntilMs = 11000;
  assert(actuator->apply(flow) == Result::Accepted);
  device.setManualFlow(47);
  assert(device.heatingState().revision == 1);
  assert(!device.heatingState().overrideActive);
  assert(actuator->apply(flow) == Result::Rejected);  // Stale computation loses.

  // A DHW-only flame must not become the room climate's heating action.
  device.status(0x0c);
  assert(actuator->snapshot().phase == Phase::Standby);
  assert(actuator->snapshot().sampledAtMs == 6000);
  nowMs = 7000;
  assert(actuator->snapshot().sampledAtMs == 6000);  // No fabricated freshness.
  device.status(0x0a);
  assert(actuator->snapshot().phase == Phase::Heating);

  // The common state lock remains held through transport send: CH changes
  // serialize behind a frame already being physically sent.
  flow.expectedRevision = 1;
  flow.validUntilMs = 12000;
  assert(actuator->apply(flow) == Result::Accepted);
  device.transport.block = true;
  device.transport.entered = false;
  device.queue(MessageType::WriteData, DataId::TSet, toF88(47));
  std::thread sender([&] { device.transmitQueued(); });
  {
    std::unique_lock lock(device.transport.mutex);
    device.transport.condition.wait(lock, [&] { return device.transport.entered; });
  }
  std::atomic<bool> changed{false};
  std::thread changer([&] { device.setGate(false); changed = true; });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  assert(!changed.load());
  {
    std::lock_guard lock(device.transport.mutex);
    device.transport.released = true;
    device.transport.condition.notify_all();
  }
  sender.join(); changer.join();
  assert(changed && !device.heatingState().enabled);
  assert(device.hvac().tDhwSet == 52);
  std::cout << "OpenTherm thermostat: transport, gate, lease, manual ownership, "
               "DHW isolation and synchronization passed\n";
}
