#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

// Match ESP32's unsigned-long millis() representation when testing retry logic.
static_assert(sizeof(unsigned long) == sizeof(uint32_t),
              "Use a host compiler with 32-bit unsigned long (Windows MinGW).");

namespace fake {
  uint64_t nowMs = 0;
  uint32_t watchdogSeconds = 30;
  bool initialized = true;
  unsigned loops = 0;
  uint32_t waitedTicks = 0;
  void (*task)(void*) = nullptr;
  struct WaitReached {};
}

using TickType_t = uint32_t;
using TaskHandle_t = void*;
constexpr int pdTRUE = 1;
// A 100 Hz tick exposes positive waits shorter than one RTOS tick.
#define pdMS_TO_TICKS(ms) ((ms) / 10)
void xTaskNotifyGive(TaskHandle_t) {}
int xTaskCreate(void (*task)(void*), const char*, int, void*, int,
                TaskHandle_t* handle) {
  fake::task = task;
  *handle = reinterpret_cast<TaskHandle_t>(1);
  return 1;
}
int esp_task_wdt_add(void*) { return 0; }
int esp_task_wdt_reset() {
  if (++fake::loops > 20)
    throw std::runtime_error("scheduler spun without reaching a wait");
  return 0;
}
uint32_t ulTaskNotifyTake(int, TickType_t ticks) {
  fake::waitedTicks = ticks;
  throw fake::WaitReached{};
}
int64_t esp_timer_get_time() { return static_cast<int64_t>(fake::nowMs * 1000); }

// Inert JSON dependencies: the complete real Device declaration is compiled,
// but these tests exercise only sensor scheduling and retry state.
struct JsonVariantConst {
  bool isUnbound() const { return true; }
};
struct JsonSlot {
  template <typename T> JsonSlot& operator=(T) { return *this; }
};
struct JsonObjectConst {
  JsonVariantConst operator[](const char*) const { return {}; }
};
struct JsonObject {
  JsonSlot operator[](const char*) const { return {}; }
  void remove(const char*) {}
  template <typename T> void set(T) {}
};
struct JsonDocument {
  template <typename T> T as() const { return {}; }
  template <typename T> T to() { return {}; }
};

namespace esp32m {
  class Event {
   public:
    explicit Event(const char* type) : _type(type) {}
    bool is(const char* type) const { return !std::strcmp(_type, type); }
    void publish();
   private:
    const char* _type;
  };
  std::function<void(Event&)> broadcast;
  void Event::publish() { if (broadcast) broadcast(*this); }
  struct EventInited {
    static bool is(Event& event) { return event.is("inited"); }
  };
  struct EventManager {
    static EventManager& instance() { static EventManager value; return value; }
    void subscribe(std::function<void(Event&)>) {}
  };
  class AppObject {
   protected:
    virtual void handleEvent(Event&) {}
    template <typename... T> void logI(const char*, T...) {}
  };
  namespace json { struct PropsContainer {}; }
  struct App {
    static bool initialized() { return fake::initialized; }
    static App& instance() { static App value; return value; }
    uint32_t wdtTimeout() const { return fake::watchdogSeconds; }
  };
  namespace net::ota { constexpr const char* Name = "ota"; }
  namespace locks { struct Guard { explicit Guard(const char*) {} }; }
  unsigned long millis() { return static_cast<uint32_t>(fake::nowMs); }
}

#include "production.inc"

namespace {
  constexpr uint64_t Wrap = uint64_t{1} << 32;
  void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
  }
  class Sensor : public esp32m::Device {
   public:
    int polls = 0;
    int initializations = 0;
    bool initOk = true;
    bool pollOk = true;
    uint64_t pollDurationMs = 0;
    Sensor() { init(HasSensors); }
    void accept(esp32m::Event& event) { handleEvent(event); }
    bool due() { return shouldPollSensors(); }
    uint64_t next() { return nextSensorsPollTime(); }
    void reset(bool immediate) { resetSensors(immediate); }
   protected:
    bool initSensors() override { ++initializations; return initOk; }
    bool pollSensors() override {
      ++polls;
      fake::nowMs += pollDurationMs;
      return pollOk;
    }
  };
  void use(std::initializer_list<Sensor*> sensors) {
    std::vector<Sensor*> copy(sensors);
    esp32m::broadcast = [copy](esp32m::Event& event) {
      for (auto* sensor : copy) sensor->accept(event);
    };
    fake::initialized = true;
    fake::watchdogSeconds = 30;
  }
  void runToWait() {
    fake::loops = 0;
    fake::waitedTicks = 0;
    try { fake::task(nullptr); }
    catch (const fake::WaitReached&) { return; }
    throw std::runtime_error("scheduler task returned unexpectedly");
  }
  void pollDirectly(Sensor& sensor) {
    esp32m::EventPollSensors event;
    sensor.accept(event);
  }
  void normalAndEquality() {
    Sensor sensor;
    use({&sensor});
    fake::nowMs = 0;
    runToWait();
    require(sensor.polls == 0 && fake::waitedTicks == 100, "startup wait");
    fake::nowMs = 999;
    runToWait();
    require(sensor.polls == 0 && fake::waitedTicks == 1, "positive sub-tick wait");
    fake::nowMs = 1000;
    runToWait();
    require(sensor.polls == 1 && sensor.next() == 2000, "poll at deadline equality");
    require(fake::waitedTicks == 100, "normal poll interval");
  }
  void rolloverAndOverdue() {
    Sensor overdue, future;
    use({&overdue, &future});
    fake::nowMs = Wrap - 1500;
    pollDirectly(overdue);
    future.setSensorsPollInterval(2000);
    pollDirectly(future);
    require(overdue.next() == Wrap - 500, "pre-rollover deadline");
    require(future.next() == Wrap + 500, "deadline spans rollover");
    fake::nowMs = Wrap - 600;
    runToWait();
    require(overdue.polls == 1 && future.polls == 1 && fake::waitedTicks == 10,
            "earliest deadline selected across rollover");
    fake::nowMs = Wrap + 100;
    runToWait();
    require(overdue.polls == 2 && future.polls == 1,
            "overdue pre-wrap sensor polls while future sensor waits");
    require(fake::waitedTicks == 40, "post-wrap future sensor wait");
    fake::nowMs = Wrap + 500;
    runToWait();
    require(future.polls == 2, "cross-wrap deadline equality");
    fake::nowMs = 5 * Wrap + 123;
    runToWait();
    require(overdue.polls == 3 && future.polls == 3,
            "both sensors recover after several missed rollover periods");
    require(overdue.next() == fake::nowMs + 1000,
            "poll completion timestamp remains 64-bit");
  }
  void completionAndRetry() {
    Sensor sensor;
    use({&sensor});
    fake::nowMs = Wrap - 10;
    sensor.pollDurationMs = 25;
    runToWait();
    require(sensor.next() == Wrap + 1015, "interval starts after poll completes");
    sensor.pollDurationMs = 0;
    sensor.initOk = false;
    sensor.reset(true);
    fake::nowMs = Wrap + 1015;
    runToWait();
    require(sensor.initializations == 2 && sensor.polls == 1, "failed init attempted");
    fake::nowMs += 1000;
    runToWait();
    require(sensor.initializations == 2, "retry delay respected");
    fake::nowMs += 9000;
    sensor.initOk = true;
    runToWait();
    require(sensor.initializations == 3 && sensor.polls == 2,
            "initialization retries resume after configured delay");
  }
  void retryAcrossRollover() {
    Sensor sensor;
    use({&sensor});
    sensor.initOk = false;
    fake::nowMs = Wrap - 5000;
    runToWait();
    fake::nowMs = Wrap + 4999;
    runToWait();
    require(sensor.initializations == 1, "retry delay crosses rollover");
    fake::nowMs = Wrap + 6000;
    sensor.initOk = true;
    runToWait();
    require(sensor.initializations == 2 && sensor.polls == 1,
            "retry eventually succeeds across rollover");
  }
  void boundsAndIntervals() {
    use({});
    fake::nowMs = 5 * Wrap;
    runToWait();
    require(fake::waitedTicks == 100, "no-device sentinel wait capped");
    fake::watchdogSeconds = 1;
    runToWait();
    require(fake::waitedTicks == 90, "watchdog seconds converted to milliseconds");
    fake::watchdogSeconds = 0;
    runToWait();
    require(fake::waitedTicks >= 1 && fake::waitedTicks <= 100,
            "zero watchdog timeout stays bounded");
    fake::initialized = false;
    fake::watchdogSeconds = 30;
    runToWait();
    require(fake::waitedTicks == 100, "uninitialized app waits");
    Sensor sensor;
    use({&sensor});
    fake::nowMs = 1000;
    sensor.setSensorsPollInterval(0);
    runToWait();
    require(sensor.getSensorsPollInterval() == 1 && fake::waitedTicks == 1,
            "zero interval normalized with finite wait");
    sensor.setSensorsPollInterval(-10);
    require(sensor.getSensorsPollInterval() == 1, "negative interval normalized");
    sensor.setSensorsPollInterval(INT_MAX);
    runToWait();
    require(fake::waitedTicks == 100, "very large interval wait capped before ticks");
  }
}

int main() {
  try {
    normalAndEquality();
    rolloverAndOverdue();
    completionAndRetry();
    retryAcrossRollover();
    boundsAndIntervals();
    std::cout << "PASS: production sensor scheduler (5 scenario groups)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
