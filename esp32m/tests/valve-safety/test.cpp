#include <ArduinoJson.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
  ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103,
  ESP_ERR_TIMEOUT = 0x107, ESP_ERR_INVALID_RESPONSE = 0x108;
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) (x)
#define ESP_CHECK_RETURN(x) do { auto result = (x); if (result != ESP_OK) return result; } while (0)
const char* esp_err_to_name(esp_err_t error) {
  switch (error) {
    case ESP_OK: return "ESP_OK";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
    default: return "error";
  }
}
using TickType_t = uint32_t;
using TaskHandle_t = void*;
constexpr int pdTRUE = 1, pdPASS = 1, tskIDLE_PRIORITY = 0;
#define pdMS_TO_TICKS(ms) ((ms) / 10)
namespace fake {
  std::atomic<uint64_t> nowMs{0};
  std::map<void*, void (*)(void*)> tasks;
  bool createTask = true;
  unsigned waits = 0, delays = 0, notifications = 0, persisted = 0;
  TickType_t lastWait = 0;
  std::function<void()> duringDelay;
  struct WaitReached {};
}
int64_t esp_timer_get_time() { return fake::nowMs.load() * 1000; }
int xTaskCreate(void (*entry)(void*), const char*, int, void* self, int,
                TaskHandle_t* task) {
  if (!fake::createTask) return 0;
  fake::tasks[self] = entry;
  *task = self;
  return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t) { ++fake::notifications; }
int esp_task_wdt_add(void*) { return ESP_OK; }
int esp_task_wdt_reset() { return ESP_OK; }
uint32_t ulTaskNotifyTake(int, TickType_t ticks) {
  ++fake::waits;
  fake::lastWait = ticks;
  throw fake::WaitReached{};
}
void vTaskDelay(TickType_t ticks) {
  if (!ticks) throw std::runtime_error("zero tick wait");
  ++fake::delays;
  fake::nowMs += ticks * 10;
  if (fake::duringDelay) fake::duringDelay();
}

namespace esp32m {
  struct ErrorList {
    int error = ESP_OK;
    void check(int value) { error = value; }
    bool empty() const { return error == ESP_OK; }
  };
  struct RequestContext {
    JsonVariantConst data;
    ErrorList errors;
  };
  struct Request {
    JsonDocument payload;
    int response = -999;
    bool is(const char*) const { return true; }
    JsonVariantConst data() const { return payload.as<JsonVariantConst>(); }
    void respond(int error) { response = error; }
  };
  struct AppObject {
    virtual ~AppObject() = default;
    virtual const char* name() const { return "test"; }
    virtual bool handleRequest(Request&) { return false; }
    template <typename... Args> void logI(const char*, Args...) {}
    template <typename... Args> void logW(const char*, Args...) {}
  };
  struct Device : AppObject {
    virtual const JsonVariantConst descriptor() const { return {}; }
    virtual JsonDocument* getState(RequestContext&) { return nullptr; }
    virtual void setState(RequestContext&) {}
    virtual bool setConfig(RequestContext&) { return false; }
    virtual JsonDocument* getConfig(RequestContext&) { return nullptr; }
  };
  namespace config {
    struct Changed { static void publish(void*) { ++fake::persisted; } };
  }
  namespace integrations::ha {
    struct CommandRequest { static constexpr const char* Name = "ha-command"; };
  }
  namespace io {
    namespace pin {
      struct IDigital {
        virtual ~IDigital() = default;
        virtual esp_err_t setDirection(bool, bool) = 0;
        virtual esp_err_t read(bool&) = 0;
      };
      struct Tx {
        enum Type { Read };
        explicit Tx(Type) {}
        esp_err_t commit() { return ESP_OK; }
      };
    }
    struct IPin { pin::IDigital* digital() { return nullptr; } };
  }
  namespace dev {
    struct Relay {
      void setPersistent(bool) {}
      esp_err_t turn(bool) { return ESP_OK; }
    };
    struct HBridge {
      enum class Mode { Off, Forward, Reverse };
      void setPersistent(bool) {}
      esp_err_t run(Mode) { return ESP_OK; }
    };
    enum class ValveState { Unknown, Opening, Open, Closing, Closed };
    struct ValveComponent {
      ValveComponent(Device*, const char*) {}
      void set(ValveState) {}
      void resetState() {}
    };
  }
}

#include "production.inc"

namespace {
  using esp32m::dev::Valve;
  using namespace esp32m::dev::valve;
  void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
  }
  struct Wiring : IWiring {
    std::vector<Cmd> commands;
    std::deque<esp_err_t> offErrors;
    esp_err_t startError = ESP_OK;
    std::atomic<unsigned> calls{0};
    esp_err_t set(Cmd command) override {
      ++calls;
      commands.push_back(command);
      if (command == Off) {
        if (offErrors.empty()) return ESP_OK;
        auto result = offErrors.front();
        offErrors.pop_front();
        return result;
      }
      return startError;
    }
    unsigned count(Cmd command) const {
      return std::count(commands.begin(), commands.end(), command);
    }
  };
  struct Sensor : ISensor {
    State position = Unknown;
    esp_err_t error = ESP_OK;
    unsigned reads = 0;
    std::function<void()> onRead;
    esp_err_t sense(State& state) override {
      ++reads;
      if (onRead) onRead();
      state = position;
      return error;
    }
  };
  struct TestValve : Valve {
    Wiring* wiring;
    Sensor* sensor;
    TestValve() : TestValve(new Wiring, new Sensor) {}
    TestValve(Wiring* w, Sensor* s) : Valve("test", w, s), wiring(w), sensor(s) {}
    JsonDocument json() {
      esp32m::RequestContext context;
      std::unique_ptr<JsonDocument> document(getState(context));
      return *document;
    }
    JsonDocument config() {
      esp32m::RequestContext context;
      std::unique_ptr<JsonDocument> document(getConfig(context));
      return *document;
    }
    int jsonCommand(const char* action) {
      JsonDocument document;
      document["state"] = action;
      esp32m::RequestContext context{document.as<JsonVariantConst>(), {}};
      setState(context);
      return context.errors.error;
    }
    bool configure(const char* action) {
      JsonDocument document;
      document["state"] = action;
      esp32m::RequestContext context{document.as<JsonVariantConst>(), {}};
      return setConfig(context);
    }
    int haCommand(const char* action) {
      esp32m::Request request;
      request.payload.set(action);
      require(handleRequest(request), "HA request not handled");
      return request.response;
    }
    void poll() {
      try { fake::tasks.at(this)(this); }
      catch (const fake::WaitReached&) { return; }
      throw std::runtime_error("worker returned without yielding");
    }
  };
  void asyncDeadlineAndWrap() {
    for (auto start : {uint64_t{0}, (uint64_t{1} << 32) - 5000}) {
      fake::nowMs = start;
      TestValve valve;
      require(valve.set(Open, false) == ESP_OK, "start open");
      fake::nowMs = start + 9999;
      valve.poll();
      require(valve.state() == Valve::Opening, "premature timeout");
      // A repeated HA/UI command must retain the original deadline.
      require(valve.set(Open, false) == ESP_OK, "repeat open");
      fake::nowMs = start + 10000;
      valve.poll();
      auto state = valve.json();
      require(state["error"] == ESP_ERR_TIMEOUT, "async deadline missing");
      require(state["fault"] == "travel-timeout", "timeout not exposed");
      require(!state["stopPending"].as<bool>(), "stop should be confirmed");
      require(valve.wiring->commands.back() == Off, "timeout left motor running");
      require(valve.state() == Valve::Unknown, "timeout retained motion state");
      require(valve.wiring->count(Open) == 1, "repeat replayed motion");
    }
  }
  void sensorFaultAndStopRecovery() {
    fake::nowMs = 0;
    TestValve valve;
    require(valve.turn(1) == ESP_OK, "turn open");
    valve.sensor->error = ESP_ERR_INVALID_RESPONSE;
    valve.wiring->offErrors = {ESP_ERR_TIMEOUT, ESP_ERR_TIMEOUT, ESP_OK};
    valve.poll();
    auto state = valve.json();
    require(state["error"] == ESP_ERR_INVALID_RESPONSE, "lost sensor cause");
    require(state["stopError"] == ESP_ERR_TIMEOUT && state["stopPending"] == true,
            "failed stop is not visible");
    const auto offCount = valve.wiring->count(Off);
    fake::nowMs = 249;
    valve.poll();
    require(valve.wiring->count(Off) == offCount, "stop retries are unbounded");
    fake::nowMs = 250;
    valve.poll();
    require(valve.wiring->count(Off) == offCount + 1, "stop retry missing");
    fake::nowMs = 500;
    valve.sensor->error = ESP_OK;
    valve.poll();
    state = valve.json();
    require(state["stopPending"] == false && state["stopError"] == ESP_OK,
            "successful stop retry not recorded");
    require(state["error"] == ESP_ERR_INVALID_RESPONSE, "recovery silently cleared fault");
    for (unsigned i = 0; i != 5; ++i) {
      fake::nowMs += 1000;
      valve.poll();
    }
    require(valve.wiring->count(Open) == 1, "recovery replayed stale open");
    require(valve.set(Close, false) == ESP_OK, "explicit command after recovery");
    require(valve.json()["error"] == ESP_OK, "new command failed to clear fault");
  }
  void failedStartAndCommandErrors() {
    fake::nowMs = 0;
    TestValve valve;
    valve.wiring->startError = ESP_ERR_TIMEOUT;
    require(valve.turn(1) == ESP_ERR_TIMEOUT, "turn swallowed failed start");
    require(valve.wiring->commands == std::vector<Cmd>{Off, Open, Off},
            "failed start was not followed by Off");
    require(valve.haCommand("close") == ESP_ERR_TIMEOUT, "HA swallowed error");
    require(valve.jsonCommand("open") == ESP_ERR_TIMEOUT, "UI swallowed error");
    require(!valve.configure("open"), "failed config command reported a persistent change");
    require(valve.haCommand("bogus") == ESP_ERR_INVALID_ARG, "HA accepted invalid command");
    require(valve.jsonCommand("bogus") == ESP_ERR_INVALID_ARG, "UI accepted invalid command");
    require(valve.turn(NAN) == ESP_ERR_INVALID_ARG && valve.turn(2) == ESP_ERR_INVALID_ARG,
            "invalid target accepted");
    valve.wiring->startError = ESP_OK;
    valve.sensor->error = ESP_ERR_INVALID_RESPONSE;
    auto starts = valve.wiring->count(Open);
    require(valve.set(Open, false) == ESP_ERR_INVALID_RESPONSE, "sensor precheck missing");
    require(valve.wiring->count(Open) == starts, "moved without valid sensing");
  }
  void endpointsAndFaultPersistence() {
    fake::nowMs = 0;
    TestValve valve;
    valve.sensor->position = ISensor::Closed;
    require(valve.set(Open, false) == ESP_OK, "open from closed");
    valve.poll();
    require(valve.state() == Valve::Opening, "departure limit ended motion immediately");
    fake::nowMs = 100;
    valve.sensor->position = ISensor::Opened;
    valve.poll();
    require(valve.state() == Valve::Opened && valve.wiring->commands.back() == Off,
            "arrival limit did not stop motor immediately");
    require(valve.set(Close, false) == ESP_OK, "close from open");
    valve.sensor->position = ISensor::Invalid;
    valve.poll();
    require(valve.json()["fault"] == "limits", "invalid simultaneous limits ignored");
    auto writes = fake::persisted;
    valve.sensor->position = ISensor::Closed;
    valve.poll();
    require(valve.state() == Valve::Closed, "recovered sensor position unavailable");
    require(fake::persisted == writes && valve.config().size() == 0,
            "fault was persisted as a successful position");
    require(valve.jsonCommand("stop") == ESP_OK, "explicit stop failed");
    require(valve.json()["error"] == ESP_OK, "explicit valid stop did not acknowledge fault");
  }
  void failedEndpointStop() {
    fake::nowMs = 1000;
    TestValve valve;
    require(valve.set(Open, false) == ESP_OK, "start");
    valve.sensor->position = ISensor::Opened;
    valve.wiring->offErrors.push_back(ESP_ERR_TIMEOUT);
    valve.poll();
    require(valve.state() == Valve::Unknown, "unconfirmed stop declared successful arrival");
    require(valve.json()["stopPending"] == true, "lost endpoint stop failure");
    fake::nowMs += 250;
    valve.poll();
    require(valve.state() == Valve::Opened && valve.json()["stopPending"] == false,
            "endpoint Off retry failed");
    require(valve.json()["error"] == ESP_ERR_TIMEOUT, "stop fault was not latched");
  }
  void synchronousWaitYieldsAndReleasesLock() {
    fake::nowMs = 0;
    fake::delays = 0;
    TestValve valve;
    require(valve.set(Open, true) == ESP_ERR_TIMEOUT, "sync timeout missing");
    require(fake::delays == 1000, "waitFor did not yield once per poll");
    bool stopOnce = true;
    fake::duringDelay = [&] {
      if (!stopOnce) return;
      stopOnce = false;
      std::thread otherCommand([&] {
        require(valve.set(Off, false) == ESP_OK, "concurrent stop failed");
      });
      otherCommand.join();  // Hangs if waitFor slept while holding the valve mutex.
    };
    require(valve.set(Open, true) == ESP_ERR_INVALID_STATE,
            "superseded blocking command reported success");
    fake::duringDelay = {};
  }
  void workerAndCommandsAreSerialized() {
    fake::nowMs = 0;
    TestValve valve;
    require(valve.set(Open, false) == ESP_OK, "start");
    std::mutex gate;
    std::condition_variable condition;
    bool sensorEntered = false, releaseSensor = false;
    valve.sensor->onRead = [&] {
      std::unique_lock lock(gate);
      if (sensorEntered) return;
      sensorEntered = true;
      condition.notify_all();
      condition.wait(lock, [&] { return releaseSensor; });
    };
    std::thread worker([&] { valve.poll(); });
    {
      std::unique_lock lock(gate);
      condition.wait(lock, [&] { return sensorEntered; });
    }
    const auto writes = valve.wiring->calls.load();
    std::thread command([&] { require(valve.set(Close, false) == ESP_OK, "concurrent close"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const bool serialized = valve.wiring->calls.load() == writes;
    {
      std::lock_guard lock(gate);
      releaseSensor = true;
    }
    condition.notify_all();
    worker.join();
    command.join();
    require(serialized, "command overlapped worker sensor access");
    require(valve.state() == Valve::Closing, "lost concurrent command");
  }
  void pinSetupRetriesAndWorkerCreationFailsClosed() {
    struct Pin : esp32m::io::pin::IDigital {
      int setups = 0, reads = 0;
      esp_err_t setupError = ESP_OK;
      bool level = true;
      esp_err_t setDirection(bool input, bool output) override {
        require(input && !output, "wrong sensor pin mode");
        ++setups;
        return setupError;
      }
      esp_err_t read(bool& value) override { ++reads; value = level; return ESP_OK; }
    } open, closed;
    closed.setupError = ESP_ERR_TIMEOUT;
    TwoPinSensor sensor(&open, &closed);
    ISensor::State state = ISensor::Opened;
    require(sensor.sense(state) == ESP_ERR_TIMEOUT && state == ISensor::Unknown,
            "failed setup reported a cached sensor state");
    require(open.reads == 0 && closed.reads == 0, "read an unconfigured sensor");
    closed.setupError = ESP_OK;
    closed.level = false;
    require(sensor.sense(state) == ESP_OK && state == ISensor::Closed,
            "sensor configuration was not retried");
    require(open.setups == 2 && closed.setups == 2, "missing direction retry");
    require(sensor.sense(state) == ESP_OK && open.setups == 2, "unnecessary reconfiguration");
    fake::createTask = false;
    TestValve valve;
    fake::createTask = true;
    require(valve.set(Open, false) == ESP_ERR_INVALID_STATE,
            "motion accepted without its safety worker");
    require(valve.wiring->count(Open) == 0 && valve.wiring->commands.back() == Off,
            "worker creation failure did not establish Off");
  }
}

int main() {
  try {
    asyncDeadlineAndWrap();
    sensorFaultAndStopRecovery();
    failedStartAndCommandErrors();
    endpointsAndFaultPersistence();
    failedEndpointStop();
    synchronousWaitYieldsAndReleasesLock();
    workerAndCommandsAreSerialized();
    pinSetupRetriesAndWorkerCreationFailsClosed();
    std::cout << "PASS: valve async/sync deadlines, faults, stop retries, command errors, "
                 "no motion replay, limits, concurrency, setup retry and 32-bit wrap\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
