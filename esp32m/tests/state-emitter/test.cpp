#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static_assert(sizeof(unsigned long) == sizeof(uint32_t),
              "Use a compiler with ESP32-sized unsigned long (Windows MinGW).");

using TickType_t = uint32_t;
using TaskHandle_t = void*;
constexpr int pdTRUE = 1;
#define pdMS_TO_TICKS(ms) ((ms) / 10)
#define ENUM_FLAG_OPERATORS(type) \
  constexpr type operator|(type a, type b) { \
    return static_cast<type>(static_cast<int>(a) | static_cast<int>(b)); \
  }

namespace fake {
  uint64_t now = 1000;
  unsigned loops = 0, notifications = 0, watchdogSeconds = 30;
  TickType_t waitTicks = 0;
  std::function<void()> task;
  struct WaitReached {};
}

int xTaskCreate(void (*fn)(void*), const char*, int, void* arg, int,
                TaskHandle_t* handle) {
  fake::task = [=] { fn(arg); };
  *handle = reinterpret_cast<TaskHandle_t>(1);
  return 1;
}
void xTaskNotifyGive(TaskHandle_t) { ++fake::notifications; }
int esp_task_wdt_add(void*) { return 0; }
int esp_task_wdt_reset() {
  if (++fake::loops > 10) throw std::runtime_error("busy loop before wait");
  return 0;
}
uint32_t ulTaskNotifyTake(int, TickType_t ticks) {
  fake::waitTicks = ticks;
  if (!ticks) throw std::runtime_error("zero-tick wait would busy-loop");
  throw fake::WaitReached{};
}

namespace esp32m {
  unsigned long millis() { return static_cast<unsigned long>(fake::now); }
  struct Event { bool inited = false; const void* component = nullptr; };
  struct EventInited { static bool is(Event& event) { return event.inited; } };
  class AppObject {
   protected:
    virtual void handleEvent(Event&) {}
  };
  struct App {
    static App& instance() { static App instance; return instance; }
    uint32_t wdtTimeout() const { return fake::watchdogSeconds; }
  };
  namespace dev {
    struct Component {
      std::string key;
      double value;
      bool disabled = false;
      std::string uid() const { return key; }
      bool isDisabled() const { return disabled; }
    };
    std::vector<Component*> registry;
    struct AllComponents {
      auto begin() const { return registry.begin(); }
      auto end() const { return registry.end(); }
    };
    struct ComponentStateChanged : Event {
      explicit ComponentStateChanged(const Component* value) { Event::component = value; }
      const Component* component() const { return static_cast<const Component*>(Event::component); }
      static bool is(Event& event, ComponentStateChanged** result) {
        if (!event.component) return false;
        *result = static_cast<ComponentStateChanged*>(&event);
        return true;
      }
    };
  }
}

#include "production.inc"

using namespace esp32m::dev;
constexpr uint64_t Wrap = uint64_t{1} << 32;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

class Emitter : public StateEmitter {
 public:
  using StateEmitter::StateEmitter;
  struct Sample { uint64_t at; std::string key; double value; };
  std::vector<Sample> samples;
  std::function<void()> duringEmit;
  const Component* excluded = nullptr;
  void start(std::initializer_list<Component*> components) {
    registry = components;
    fake::notifications = 0;
    esp32m::Event event{true};
    handleEvent(event);
  }
  void changed(Component& component) {
    ComponentStateChanged event(&component);
    handleEvent(event);
  }
  void step(uint64_t at) {
    fake::now = at;
    fake::loops = 0;
    fake::waitTicks = 0;
    try { fake::task(); } catch (fake::WaitReached&) {}
    require(fake::waitTicks > 0, "task must reach blocking wait");
  }
  std::vector<uint64_t> times(const Component& component) const {
    std::vector<uint64_t> result;
    for (const auto& sample : samples)
      if (sample.key == component.key) result.push_back(sample.at);
    return result;
  }
 protected:
  bool filter(const Component* component) override { return component != excluded; }
  void emit(std::vector<const Component*> components) override {
    for (auto* component : components)
      samples.push_back({fake::now, component->uid(), component->value});
    if (duringEmit) duringEmit();
  }
};

void periodicAndBusy() {
  Component stable{"bfs_current", 1.9}, busy{"other_frequency", 37.56};
  Emitter emitter;
  emitter.start({&stable, &busy});
  emitter.step(1000);
  require(fake::waitTicks == 100, "default periodic interval is one second");
  for (uint64_t at = 1100; at <= 5000; at += 100) {
    busy.value += .01;
    emitter.changed(busy);
    emitter.step(at);
  }
  require(emitter.times(stable) == std::vector<uint64_t>({1000, 2000, 3000, 4000, 5000}),
          "frequent changes starved the stable sensor's periodic refresh");
  require(emitter.times(busy).size() == 41, "changes must still emit immediately, without duplicates");
  require(fake::notifications == 40, "on-change notification missing");
  for (const auto& sample : emitter.samples)
    if (sample.key == stable.key) require(sample.value == 1.9, "stable sample changed");
  emitter.step(6000);
  require(emitter.times(stable).back() == 6000, "idle sensor must also refresh");
}

void earlyWakesAndWatchdog() {
  Component stable{"bfs_current", 1.9};
  Emitter emitter;
  emitter.setInterval(3000);
  emitter.start({&stable});
  emitter.step(3000);
  require(fake::waitTicks <= 100, "long intervals must wake for watchdog servicing");
  emitter.step(4000);
  emitter.step(5000);
  require(emitter.samples.size() == 1, "watchdog wakes must not emit early");
  emitter.step(5999);
  require(fake::waitTicks == 1, "sub-tick remaining time must block for a tick");
  emitter.step(6009);
  require(emitter.times(stable) == std::vector<uint64_t>({3000, 6009}),
          "empty early wakes postponed periodic emission");
}

void modesAndFilters() {
  Component stable{"bfs_current", 1.9}, disabled{"disabled", 1}, hidden{"filtered", 2};
  disabled.disabled = true;
  Emitter emitter(EmitFlags::OnChange);
  emitter.excluded = &hidden;
  emitter.start({&stable, &disabled, &hidden});
  emitter.step(1000);
  emitter.step(2000);
  require(emitter.samples.empty(), "change-only mode must not emit unchanged sensors");
  emitter.changed(stable);
  emitter.changed(stable);
  emitter.changed(disabled);
  emitter.changed(hidden);
  emitter.step(2100);
  require(emitter.samples.size() == 1 && fake::notifications == 2,
          "change-only queue must coalesce and filter");
  emitter.step(3100);
  require(emitter.samples.size() == 1, "change-only mode repeated an unchanged sensor");
  emitter.changed(stable);
  stable.disabled = true;
  emitter.step(3200);
  require(emitter.samples.size() == 1, "component disabled after queuing was emitted");
  stable.disabled = false;

  Emitter periodic(EmitFlags::Periodically);
  periodic.excluded = &hidden;
  periodic.start({&stable, &disabled, &hidden});
  periodic.step(4000);
  periodic.changed(stable);
  require(fake::notifications == 0, "periodic-only mode must not notify on change");
  periodic.step(5000);
  require(periodic.times(stable) == std::vector<uint64_t>({4000, 5000}) &&
          periodic.samples.size() == 2, "periodic-only scan duplicated or ignored filtering");
}

void rolloverAndSlowEmit() {
  Component stable{"bfs_current", 1.9}, busy{"other_frequency", 37.56};
  Emitter emitter;
  emitter.start({&stable, &busy});
  const auto first = Wrap - 500;
  emitter.step(first);
  require(fake::waitTicks == 100, "future deadline across millis rollover spun");
  for (uint64_t at = first + 100; at <= first + 2000; at += 100) {
    emitter.changed(busy);
    emitter.step(at);
  }
  require(emitter.times(stable) == std::vector<uint64_t>({first, first + 1000, first + 2000}),
          "millis rollover lost periodic refresh");

  Emitter slow;
  slow.start({&stable});
  slow.duringEmit = [] { fake::now += 1500; };
  slow.step(1000);
  require(fake::waitTicks == 100, "slow output must not produce catch-up bursts");
  slow.step(3500);
  require(slow.times(stable) == std::vector<uint64_t>({1000, 3500}),
          "periodic interval should be measured from completion");
}

void intervalAndWatchdogBounds() {
  Component stable{"bfs_current", 1.9};
  for (const auto interval : {0, -1}) {
    Emitter emitter;
    emitter.setInterval(interval);
    require(emitter.getInterval() == 1, "nonpositive interval must be clamped");
    emitter.start({&stable});
    emitter.step(1000);
    require(fake::waitTicks == 1, "minimum interval must block for a tick");
  }
  Emitter emitter;
  emitter.setInterval(60000);
  emitter.start({&stable});
  fake::watchdogSeconds = 1;
  emitter.step(60000);
  require(fake::waitTicks == 90, "watchdog seconds must be converted to milliseconds");
  fake::watchdogSeconds = 30;
}

void overriddenDeadline() {
  struct CustomEmitter : Emitter {
    unsigned long nextTime() override { return 1500; }
  } emitter;
  Component stable{"bfs_current", 1.9};
  emitter.start({&stable});
  emitter.step(1000);
  require(emitter.samples.empty(), "shouldEmit must honor the virtual deadline");
  emitter.step(1500);
  require(emitter.samples.size() == 1, "custom deadline did not emit at equality");
}

int main() {
  try {
    periodicAndBusy();
    earlyWakesAndWatchdog();
    modesAndFilters();
    rolloverAndSlowEmit();
    intervalAndWatchdogBounds();
    overriddenDeadline();
    std::cout << "StateEmitter: 6 production-source scenario groups passed\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
