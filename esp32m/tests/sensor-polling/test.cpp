#include <ArduinoJson.h>
#include <atomic>
#include <future>
#include <condition_variable>
#include <chrono>
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
  std::atomic<unsigned> invalidations{0};
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

constexpr int ESP_ERR_INVALID_STATE = 259, ESP_ERR_INVALID_ARG = 258;
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
  struct Request {
    std::string command;
    bool internal = false;
    JsonDocument payload, response;
    int error = 0;
    explicit Request(const char* command = "config-set", bool internal = false)
        : command(command), internal(internal) {}
    bool is(const char* value) const { return command == value; }
    bool isInternal() const { return internal; }
    void respond(JsonVariantConst value, bool) { response.set(value); }
    void respondError(int value, const char*) { error = value; }
  };
  struct RequestContext {
    Request& request;
    JsonVariantConst data;
    struct { int error = 0; void check(int value, const char*) { error = value; } } errors;
    std::unique_ptr<JsonDocument> result;
    RequestContext(Request& request) : request(request), data(request.payload.as<JsonVariantConst>()) {}
  };
  class AppObject {
   protected:
    static constexpr const char* KeyStateGet = "state-get";
    static constexpr const char* KeyInfoGet = "info-get";
    virtual bool handleRequest(Request&) { return false; }
    virtual JsonDocument* getConfig(RequestContext&) { return nullptr; }
    virtual bool setConfig(RequestContext&) { return false; }
    virtual void handleEvent(Event&) {}
    template <typename... T> void logI(const char*, T...) {}
  };
  struct EventStateChanged {
    static void publish(AppObject*) { fake::invalidations.fetch_add(1); }
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
    int configReads = 0, configWrites = 0;
    std::function<void()> duringPoll;
    Sensor(bool canDisable = false) { init(static_cast<Flags>(HasSensors | (canDisable ? CanDisable : 0))); }
    using Device::setConfig;
    using Device::getConfig;
    using Device::handleRequest;
    void hold(const std::function<void()>& fn) { std::lock_guard guard(_deviceMutex); fn(); }
    void accept(esp32m::Event& event) { handleEvent(event); }
    bool due() { return shouldPollSensors(); }
    uint64_t next() { return nextSensorsPollTime(); }
    void reset(bool immediate) { resetSensors(immediate); }
   protected:
    JsonDocument* getDeviceConfig(esp32m::RequestContext&) override {
      ++configReads;
      auto doc = new JsonDocument();
      (*doc)["address"] = 5;
      return doc;
    }
    bool setDeviceConfig(esp32m::RequestContext&) override { ++configWrites; return false; }
    bool initSensors() override { ++initializations; return initOk; }
    bool pollSensors() override {
      ++polls;
      if (duringPoll) duringPoll();
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
    const auto generation = sensor.sampleGeneration();
    sensor.pollOk = false;
    fake::nowMs = sensor.next();
    runToWait();
    require(sensor.sampleGeneration() != generation,
            "known failed sensor poll invalidates previously acquired samples immediately");
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
  void disabledDevice() {
    using namespace esp32m;
    Sensor sensor(true);
    use({&sensor});
    fake::nowMs = Wrap + 1000;
    sensor.setComponentState("test", 42);
    Request disable;
    disable.payload["_disabled"] = true;
    RequestContext context(disable);
    require(sensor.setConfig(context) && sensor.isDisabled(), "disable is stored");
    require(context.result && (*context.result)["_disabled"] == true && sensor.configWrites == 0,
            "toggle acknowledged without driver config or bus work");
    require(sensor.getComponentState("test").isUnbound(), "disable discards cached sensor value");
    runToWait();
    require(sensor.polls == 0 && sensor.initializations == 0 && !sensor.due() && fake::waitedTicks == 100,
            "disabled device neither initializes nor polls nor causes busy looping");
    Request query("device-state");
    require(sensor.handleRequest(query) && query.response["_disabled"] == true, "local metadata query");
    for (const auto* command : {"state-set", "run", "probe", "parameters"}) {
      Request request(command);
      require(sensor.handleRequest(request) && request.error == ESP_ERR_INVALID_STATE, "disabled commands blocked");
    }
    Request bad;
    bad.payload["_disabled"] = "true";
    RequestContext badContext(bad);
    require(!sensor.setConfig(badContext) && badContext.errors.error, "strict boolean");
    bad.payload["_disabled"] = false;
    bad.payload["address"] = 10;
    RequestContext mixed(bad);
    require(!sensor.setConfig(mixed) && mixed.errors.error && sensor.isDisabled(), "mixed toggle and hardware write rejected atomically");
    bad.payload.remove("_disabled");
    RequestContext hardware(bad);
    require(!sensor.setConfig(hardware) && hardware.errors.error && sensor.configWrites == 0, "disabled hardware settings blocked");
    Request save("config-get", true);
    RequestContext savedContext(save);
    std::unique_ptr<JsonDocument> saved(sensor.getConfig(savedContext));
    require((*saved)["_disabled"] == true && (*saved)["address"] == 5, "persistence includes both shared and device settings");
    Sensor restored(true);
    Request restore("config-set", true);
    restore.payload.set(*saved);
    RequestContext restoredContext(restore);
    require(restored.setConfig(restoredContext) && restored.isDisabled() && restored.configWrites == 1,
            "reboot restores disabled before driver settings");
    pollDirectly(restored);
    require(restored.initializations == 0, "restored disabled device remains silent");
    disable.payload["_disabled"] = false;
    RequestContext enable(disable);
    require(sensor.setConfig(enable), "re-enable changes config");
    runToWait();
    require(sensor.polls == 1 && sensor.initializations == 1, "re-enable immediately initializes and polls");
    Sensor legacy;
    RequestContext ignored(disable);
    legacy.setConfig(ignored);
    std::unique_ptr<JsonDocument> legacyConfig(legacy.getConfig(savedContext));
    require((*legacyConfig)["_disabled"].isUnbound(), "unmigrated drivers keep old config shape");
  }
  void disableWaitsForPoll() {
    using namespace esp32m;
    Sensor sensor(true);
    fake::nowMs = 2000;
    std::promise<void> entered, release;
    auto released = release.get_future();
    sensor.duringPoll = [&] { entered.set_value(); released.wait(); };
    auto poll = std::async(std::launch::async, [&] { pollDirectly(sensor); });
    entered.get_future().wait();
    auto disabling = std::async(std::launch::async, [&] {
      Request request;
      request.payload["_disabled"] = true;
      RequestContext context(request);
      return sensor.setConfig(context);
    });
    const bool waited = disabling.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout;
    release.set_value();
    poll.get();
    require(waited && disabling.get(), "disable waits until an in-flight poll completes");
    sensor.duringPoll = {};
    fake::nowMs += 10000;
    pollDirectly(sensor);
    require(sensor.polls == 1, "no polls after disable acknowledgment");
  }
  void busyDeviceDoesNotBlockOthers() {
    // Applies equally to drivers that do not expose the disable setting.
    Sensor busy, other;
    fake::nowMs = 2000;
    use({&busy, &other});
    std::promise<void> entered, release;
    auto released = release.get_future();
    auto operation = std::async(std::launch::async, [&] {
      busy.hold([&] { entered.set_value(); released.wait(); });
    });
    entered.get_future().wait();
    auto polling = std::async(std::launch::async, [&] { runToWait(); });
    const bool completed = polling.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready;
    release.set_value(); operation.get(); polling.get();
    require(completed && busy.polls == 0 && other.polls == 1,
            "busy device does not block another device or watchdog servicing");
  }
  void observationInvalidations() {
    using namespace esp32m;
    Sensor sensor(true);
    fake::nowMs = 2000;
    fake::invalidations = 0;
    pollDirectly(sensor);
    require(fake::invalidations == 1, "complete sample invalidates once");
    pollDirectly(sensor);
    require(fake::invalidations == 1, "non-due poll does not invalidate");
    fake::nowMs = sensor.next();
    pollDirectly(sensor);
    require(fake::invalidations == 2, "equal successful sample renews observation");
    sensor.pollOk = false;
    fake::nowMs = sensor.next();
    pollDirectly(sensor);
    require(fake::invalidations == 3, "failed poll publishes diagnostic/freshness hint");
    sensor.initOk = false;
    fake::nowMs = sensor.next() + 10000;
    pollDirectly(sensor);
    require(fake::invalidations == 4, "failed initialization also invalidates");
    Request disable;
    disable.payload["_disabled"] = true;
    RequestContext disabling(disable);
    sensor.setConfig(disabling);
    require(fake::invalidations == 5, "disable clears and invalidates cached values");
    sensor.setConfig(disabling);
    require(fake::invalidations == 5, "unchanged disabled setting emits no extra hint");
    disable.payload["_disabled"] = false;
    RequestContext enabling(disable);
    sensor.setConfig(enabling);
    require(fake::invalidations == 6, "re-enable invalidates metadata immediately");
  }
  void stateCaptureWaitsForPoll() {
    using namespace esp32m;
    Sensor sensor;
    fake::nowMs = 2000;
    std::promise<void> entered, release, readStarted;
    auto released = release.get_future();
    sensor.duringPoll = [&] { entered.set_value(); released.wait(); };
    auto polling = std::async(std::launch::async, [&] { pollDirectly(sensor); });
    entered.get_future().wait();
    auto reading = std::async(std::launch::async, [&] {
      Request request("state-get");
      readStarted.set_value();
      sensor.handleRequest(request);
    });
    readStarted.get_future().wait();
    const bool waited = reading.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout;
    release.set_value(); polling.get(); reading.get();
    require(waited, "state capture waits for a complete multi-component sample without CanDisable");
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
    disabledDevice();
    disableWaitsForPoll();
    busyDeviceDoesNotBlockOthers();
    observationInvalidations();
    stateCaptureWaitsForPoll();
    std::cout << "PASS: production sensor scheduler, observation and Device disable (10 scenario groups)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
