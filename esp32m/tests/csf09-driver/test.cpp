#include <ArduinoJson.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
                    ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103,
                    ESP_ERR_NOT_SUPPORTED = 0x106, ESP_ERR_TIMEOUT = 0x107,
                    ESP_ERR_INVALID_RESPONSE = 0x108;
#define ESP_CHECK_RETURN(expr) \
  do { const auto error = (expr); if (error != ESP_OK) return error; } while (0)
#define ESP_CHECK_RETURN_BOOL(expr) \
  do { if ((expr) != ESP_OK) return false; } while (0)
template<class... T> void fakeLog(T...) {}
#define logW(...) fakeLog(__VA_ARGS__)
#define logI(...) fakeLog(__VA_ARGS__)
constexpr int UART_PARITY_DISABLE = 0, MB_RTU = 0;

namespace fake {
std::atomic<uint64_t> now{1000};
std::function<void()> onSensorSet;
unsigned persisted = 0;
}
int64_t esp_timer_get_time() { return fake::now.load() * 1000; }
esp_err_t esp_task_wdt_status(void*) { return ESP_OK; }
void esp_task_wdt_reset() {}

namespace esp32m {
struct Request {
  JsonDocument payload;
  bool internal = false, broadcast = false;
  explicit Request(bool isInternal = false) : internal(isInternal) {}
  bool isInternal() const { return internal; }
  bool isBroadcast() const { return broadcast; }
  JsonVariantConst data() const { return payload.as<JsonVariantConst>(); }
};
struct ErrorList {
  esp_err_t error = ESP_OK;
  std::string message;
  void check(esp_err_t value, const char* format = nullptr, ...) {
    if (value == ESP_OK) return;
    error = value;
    if (format) {
      char buffer[768];
      va_list args;
      va_start(args, format);
      vsnprintf(buffer, sizeof(buffer), format, args);
      va_end(args);
      message = buffer;
    }
  }
  bool empty() const { return error == ESP_OK; }
};
struct RequestContext {
  Request& request;
  JsonVariantConst data;
  ErrorList errors;
  explicit RequestContext(Request& value) : request(value), data(value.data()) {}
};
struct Sensor;
struct Device {
  enum Flags { HasSensors = 1, CanDisable = 2 };
  int flags = 0;
  bool disabledForTest = false;
  std::vector<Sensor*> sensors;
  virtual ~Device() = default;
  virtual const char* name() const { return "fake"; }
  virtual bool initSensors() { return false; }
  virtual bool pollSensors() { return false; }
  virtual JsonDocument* getState(RequestContext&) { return nullptr; }
  virtual JsonDocument* getDeviceConfig(RequestContext&) { return nullptr; }
  virtual bool setDeviceConfig(RequestContext&) { return false; }
  void init(int value) { flags = value; }
  bool isDisabled() const { return disabledForTest; }
  void resetSensors(bool = false) {}
};
enum class StateClass { Undefined, Measurement };
struct Sensor {
  std::string type;
  int precision = 0;
  const char* unit = nullptr;
  StateClass stateClass = StateClass::Undefined;
  JsonDocument value;
  unsigned updates = 0;
  Sensor(Device* device, const char* type, const char* = nullptr) : type(type) {
    device->sensors.push_back(this);
  }
  JsonVariantConst get() const { return value.as<JsonVariantConst>(); }
  void resetState() { value.clear(); }
  void set(float next) {
    value.set(next);
    ++updates;
    if (fake::onSensorSet) {
      auto callback = std::move(fake::onSensorSet);
      fake::onSensorSet = nullptr;
      callback();
    }
  }
};
namespace config {
struct Changed {
  static void publish(void*, bool = false) { ++fake::persisted; }
};
}
}

namespace fake {
struct Transaction {
  uint8_t address;
  int command;
  uint16_t reg, count, raw;
  bool retry;
};
struct Drive {
  uint16_t moisture = 315, temperature = 650;
  uint8_t address = 1;
  uint32_t requestMs = 15;
  esp_err_t result = ESP_OK;
  esp_err_t ackError = ESP_OK;
  bool offline = false, offlineAfterWrite = false, ignoreWrite = false;
  std::map<uint8_t, esp_err_t> measurementErrors;
  std::map<uint8_t, std::pair<uint16_t, uint16_t>> measurementReplies;
  std::function<void(const Transaction&)> onRequest;
  std::vector<Transaction> transactions;
  unsigned writes() const {
    unsigned result = 0;
    for (const auto& transaction : transactions)
      if (transaction.command != 3) ++result;
    return result;
  }
} drive;
}
namespace esp32m::modbus {
enum Command { ReadHolding = 3, WriteRegister = 6, WriteMultiple = 16 };
struct Serial {
  uint32_t baudrate = 9600;
  int parity = UART_PARITY_DISABLE, mode = MB_RTU, data_bits = 8,
      stop_bits = 1;
};
struct Config { Serial ser_opts; };
struct Master {
  bool running = true;
  bool configured = true;
  Config settings;
  unsigned starts = 0, stops = 0, retunes = 0;
  esp_err_t startResult = ESP_OK;
  static Master& instance() { static Master master; return master; }
  bool isRunning() const { return running; }
  bool isConfigured() const { return configured; }
  const Config& config() const { return settings; }
  esp_err_t start() {
    ++starts;
    if (startResult == ESP_OK) running = true;
    return startResult;
  }
  esp_err_t stop() { ++stops; running = false; return ESP_OK; }
  esp_err_t setBaudrate(uint32_t) { ++retunes; return ESP_OK; }
  esp_err_t request(uint8_t address, Command command, uint16_t reg,
                    uint16_t count, void* buffer, bool retry = true,
                    uint32_t = 0) {
    auto& drive = fake::drive;
    auto values = static_cast<uint16_t*>(buffer);
    const auto transaction = fake::Transaction{address, command, reg, count,
        static_cast<uint16_t>(command == WriteRegister ? values[0] : 0), retry};
    drive.transactions.push_back(transaction);
    if (address < 1 || address > 247)
      throw std::runtime_error("CSF09 used a non-unicast address");
    if (drive.onRequest) drive.onRequest(transaction);
    fake::now.fetch_add(drive.requestMs);
    if (reg == 0x000f && count == 1) {
      if (retry) throw std::runtime_error("CSF09 ID write must not retry");
      if (command == ReadHolding) {
        throw std::runtime_error("Regression: CSF09 address register 0x000F is not readable");
      }
      if (command != WriteRegister)
        throw std::runtime_error("CSF09 used unsupported write function");
      if (drive.offline || address != drive.address) return ESP_ERR_TIMEOUT;
      if (!drive.ignoreWrite) drive.address = values[0];
      if (drive.offlineAfterWrite) drive.offline = true;
      return drive.ackError;
    }
    if (command != ReadHolding || reg != 0 || count != 2)
      throw std::runtime_error("CSF09 used an undocumented register/count");
    if (drive.measurementErrors.count(address)) return drive.measurementErrors[address];
    if (drive.measurementReplies.count(address)) {
      values[0] = drive.measurementReplies[address].first;
      values[1] = drive.measurementReplies[address].second;
      return ESP_OK;
    }
    if (drive.offline || address != drive.address) return ESP_ERR_TIMEOUT;
    // Deliberately fill even failed replies: callers must respect the result.
    values[0] = drive.moisture;
    values[1] = drive.temperature;
    return drive.result;
  }
};
}

#include "production.inc"

struct TestCsf09 : esp32m::dev::Csf09 {
  using Csf09::Csf09;
  using Csf09::getState;
  using Csf09::initSensors;
  using Csf09::pollSensors;
  using Csf09::getDeviceConfig;
  using Csf09::setDeviceConfig;
};
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
  check(std::isfinite(actual) && std::abs(actual - expected) < 0.0001, message);
}
void reset(uint64_t now = 1000) {
  fake::now = now;
  fake::drive = fake::Drive{};
  fake::onSensorSet = nullptr;
  fake::persisted = 0;
  esp32m::modbus::Master::instance() = esp32m::modbus::Master{};
}
JsonDocument state(TestCsf09& device) {
  esp32m::Request request;
  esp32m::RequestContext context(request);
  std::unique_ptr<JsonDocument> snapshot(device.getState(context));
  check(snapshot && snapshot->is<JsonArray>() && snapshot->size() == 6,
        "state must contain [sampledAtMs,address,moisture,temperature,pending,error]");
  return *snapshot;
}
JsonDocument settings(TestCsf09& device, bool internal = true) {
  esp32m::Request request(internal);
  esp32m::RequestContext context(request);
  std::unique_ptr<JsonDocument> snapshot(device.getDeviceConfig(context));
  check(snapshot && snapshot->is<JsonObject>(), "configuration snapshot missing");
  return *snapshot;
}
void expectAddress(TestCsf09& device, unsigned address, unsigned pending = 0) {
  const auto config = settings(device);
  check(config["address"].as<unsigned>() == address &&
            config["baud"].as<unsigned>() == 9600, "configuration tuple");
  check(pending ? config["pendingAddress"].as<unsigned>() == pending :
                  config["pendingAddress"].isUnbound(), "pending configuration tuple");
  const auto snapshot = state(device);
  check(snapshot[1].as<unsigned>() == address && snapshot[4].as<bool>() == (pending != 0),
        "state pending/current address");
}
void expectValues(const JsonDocument& snapshot, double moisture, double temperature) {
  check(!snapshot[2].isNull() && !snapshot[3].isNull(), "accepted values are null");
  near(snapshot[2].as<double>(), moisture, "moisture conversion");
  near(snapshot[3].as<double>(), temperature, "offset temperature conversion");
}
void unknownAndMetadata() {
  reset();
  TestCsf09 device;
  check(std::string(device.name()) == "CSF09", "default name");
  check(device.flags == (esp32m::Device::HasSensors | esp32m::Device::CanDisable),
        "sensor/disable flags");
  check(device.sensors.size() == 2, "two sensor components required");
  const auto moisture = device.sensors[0], temperature = device.sensors[1];
  check(moisture->type == "moisture" && temperature->type == "temperature",
        "component types");
  check(moisture->precision == 1 && temperature->precision == 1 &&
            std::string(moisture->unit) == "%" &&
            std::string(temperature->unit) == "°C" &&
            moisture->stateClass == esp32m::StateClass::Measurement &&
            temperature->stateClass == esp32m::StateClass::Measurement,
        "sensor metadata");
  auto snapshot = state(device);
  check(snapshot[0].as<int64_t>() == -1 && snapshot[1].as<unsigned>() == 1 &&
            snapshot[2].isNull() && snapshot[3].isNull() &&
            !snapshot[4].as<bool>() && snapshot[5].as<int>() == ESP_OK,
        "unseen device reported valid values");
  fake::drive.result = ESP_ERR_TIMEOUT;
  check(!device.pollSensors(), "failed first read accepted");
  snapshot = state(device);
  check(snapshot[0].as<int64_t>() == -1 && snapshot[2].isNull() && snapshot[3].isNull(),
        "failed first read manufactured a sample");
  check(!moisture->updates && !temperature->updates, "failed read published sensors");
  TestCsf09 empty(1, "");
  check(std::string(empty.name()) == "CSF09", "empty name default");
}
void manualExampleAndEndpoints() {
  reset();
  fake::drive.address = 17;
  TestCsf09 device(17, "soil-bed-2");
  check(std::string(device.name()) == "soil-bed-2", "custom name");
  check(device.initSensors() && device.pollSensors(), "manual example poll");
  auto snapshot = state(device);
  check(snapshot[0].as<uint64_t>() == fake::now.load() && snapshot[1].as<unsigned>() == 17,
        "fresh example address/timestamp");
  expectValues(snapshot, 31.5, 15);
  check(fake::drive.transactions.size() == 1 &&
            fake::drive.transactions[0].address == 17 &&
            fake::drive.transactions[0].retry,
        "poll did not use configured address");
  const auto reads = fake::drive.transactions.size();
  (void)state(device);
  check(fake::drive.transactions.size() == reads, "snapshot accessed the bus");
  for (const auto raw : {uint16_t(0), uint16_t(500), uint16_t(1000)}) {
    fake::drive.moisture = raw;
    fake::drive.temperature = raw;
    check(device.pollSensors(), "documented raw endpoint rejected");
    expectValues(state(device), raw / 10.0, raw / 10.0 - 50.0);
  }
  std::unique_ptr<esp32m::dev::Csf09> defaultFactory(esp32m::dev::useCsf09());
  std::unique_ptr<esp32m::dev::Csf09> namedFactory(esp32m::dev::useCsf09(247, "far-bed"));
  check(std::string(defaultFactory->name()) == "CSF09" &&
            std::string(namedFactory->name()) == "far-bed", "factory arguments");
}
void rejectedSamplesPreserveCache() {
  reset();
  TestCsf09 device;
  check(device.pollSensors(), "baseline poll");
  const auto stamp = fake::now.load();
  for (const auto invalid : {uint16_t(1001), uint16_t(32768), uint16_t(65535)}) {
    for (const auto field : {0, 1}) {
      fake::drive.moisture = field == 0 ? invalid : 999;
      fake::drive.temperature = field == 1 ? invalid : 1;
      fake::now.fetch_add(60001);
      check(!device.pollSensors(), "out-of-range raw sample accepted");
      const auto snapshot = state(device);
      expectValues(snapshot, 31.5, 15);
      check(snapshot[0].as<uint64_t>() == stamp,
            "invalid sample refreshed timestamp");
      check(snapshot[5].as<int>() == ESP_ERR_INVALID_RESPONSE,
            "invalid sample absent from communication status");
    }
  }
  for (const auto error : {ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE, ESP_FAIL}) {
    fake::drive.result = error;
    fake::drive.moisture = 1000;
    fake::drive.temperature = 0;
    fake::now.fetch_add(123);
    check(!device.pollSensors(), "transport failure accepted");
    const auto snapshot = state(device);
    expectValues(snapshot, 31.5, 15);
    check(snapshot[0].as<uint64_t>() == stamp,
          "transport failure refreshed timestamp");
    check(snapshot[5].as<int>() == error, "transport failure absent from state");
  }
  check(device.sensors[0]->updates == 1 && device.sensors[1]->updates == 1,
        "rejected sample partially published components");
  fake::drive.result = ESP_OK;
  fake::drive.moisture = 420;
  fake::drive.temperature = 501;
  check(device.pollSensors(), "recovery poll");
  const auto recovered = state(device);
  expectValues(recovered, 42, 0.1);
  check(recovered[0].as<uint64_t>() == fake::now.load(), "recovery did not refresh sample timestamp");
  check(recovered[5].as<int>() == ESP_OK, "valid sample did not clear last error");

  reset();
  TestCsf09 unseen;
  fake::drive.moisture = 1001;
  check(!unseen.pollSensors(), "invalid first sample accepted");
  const auto unknown = state(unseen);
  check(unknown[0].as<int64_t>() == -1 && unknown[2].isNull() && unknown[3].isNull(),
        "invalid first sample manufactured known state");
}
void timestampAtBootAndLongUptime() {
  // A valid reading at monotonic zero must remain distinguishable from unseen.
  reset(0);
  fake::drive.requestMs = 0;
  TestCsf09 boot;
  check(boot.pollSensors(), "boot poll");
  auto snapshot = state(boot);
  check(snapshot[0].as<int64_t>() == 0, "first accepted sample timestamp");
  fake::now.fetch_add((uint64_t(1) << 32) + 123);
  snapshot = state(boot);
  check(snapshot[0].as<uint64_t>() == 0,
        "cached acquisition timestamp changed at 32-bit millis rollover");
  expectValues(snapshot, 31.5, 15);
  fake::now = (uint64_t(1) << 33) - 10;
  check(boot.pollSensors(), "long-uptime sample");
  fake::now.fetch_add(60001);
  snapshot = state(boot);
  check(snapshot[0].as<uint64_t>() == (uint64_t(1) << 33) - 10,
        "long-uptime acquisition timestamp or retained stale sample changed");
  expectValues(snapshot, 31.5, 15);
}
void sharedBusLifecycleAndAddressValidation() {
  reset();
  auto& master = esp32m::modbus::Master::instance();
  TestCsf09 device;
  check(device.initSensors() && device.initSensors(), "running master init");
  check(master.starts == 0 && master.stops == 0 && master.retunes == 0 &&
            fake::drive.transactions.empty() && fake::now.load() == 1000,
        "init disturbed running shared master");
  master.running = false;
  check(device.pollSensors() && master.starts == 1 && master.running,
        "poll could not start stopped compatible master");
  master.running = false;
  check(device.initSensors() && master.running && master.starts == 2 &&
            master.stops == 0 && master.retunes == 0,
        "stopped master not started safely");
  check(device.initSensors() && master.starts == 2, "init restarted active master");
  master.running = false;
  master.startResult = ESP_ERR_TIMEOUT;
  check(!device.initSensors() && master.starts == 3 && !master.running,
        "master start failure accepted");
  for (const auto address : {uint8_t(0), uint8_t(248), uint8_t(255)}) {
    reset();
    esp32m::modbus::Master::instance().running = false;
    TestCsf09 invalid(address);
    check(!invalid.initSensors() && !invalid.pollSensors() &&
              esp32m::modbus::Master::instance().starts == 0 &&
              fake::drive.transactions.empty(), "invalid address accessed the bus");
  }
  reset();
  fake::drive.address = 247;
  TestCsf09 lastAddress(247);
  check(lastAddress.initSensors() && lastAddress.pollSensors() &&
            fake::drive.transactions[0].address == 247, "last unicast address rejected");
}
void coherentConcurrentSnapshot() {
  reset();
  TestCsf09 device;
  check(device.pollSensors(), "concurrency baseline");
  fake::drive.moisture = 850;
  fake::drive.temperature = 300;
  std::promise<void> midway, release;
  auto midwayReady = midway.get_future();
  auto releaseReady = release.get_future().share();
  fake::onSensorSet = [&] {
    midway.set_value();
    releaseReady.wait();
  };
  auto poll = std::async(std::launch::async, [&] { return device.pollSensors(); });
  const auto reached = midwayReady.wait_for(std::chrono::seconds(2));
  if (reached != std::future_status::ready) {
    release.set_value();
    (void)poll.get();
    check(false, "poll did not publish first component");
  }
  auto snapshot = std::async(std::launch::async, [&] { return state(device); });
  const bool waited = snapshot.wait_for(std::chrono::milliseconds(50)) ==
                      std::future_status::timeout;
  release.set_value();
  check(poll.get(), "concurrent poll failed");
  const auto result = snapshot.get();
  check(waited, "snapshot could observe a partly published sample");
  expectValues(result, 85, -20);
}
void configurationValidationAndRestore() {
  for (const char* payload : {R"({"address":0})", R"({"address":248})",
      R"({"address":-1})", R"({"address":1.5})", R"({"address":true})",
      R"({"address":"2"})", R"({"address":null})", R"({"baud":4800})",
      R"({"baud":19200})", R"({"baud":"9600"})", R"({"baud":true})",
      R"({"baud":null})", R"({"address":2,"baud":4800})"}) {
    reset();
    TestCsf09 device;
    esp32m::Request request;
    check(!deserializeJson(request.payload, payload), "invalid test JSON");
    esp32m::RequestContext context(request);
    check(!device.setDeviceConfig(context) && context.errors.error == ESP_ERR_INVALID_ARG &&
              fake::drive.transactions.empty(), "invalid config accessed hardware");
  }
  reset();
  TestCsf09 device;
  esp32m::Request broadcast;
  broadcast.broadcast = true;
  broadcast.payload["address"] = 2;
  esp32m::RequestContext broadcastContext(broadcast);
  check(!device.setDeviceConfig(broadcastContext) &&
            broadcastContext.errors.error != ESP_OK &&
            fake::drive.transactions.empty(), "broadcast config accepted");
  TestCsf09 invalidDefault(0);
  esp32m::Request missingAddress;
  missingAddress.payload["baud"] = 9600;
  esp32m::RequestContext missingContext(missingAddress);
  check(!invalidDefault.setDeviceConfig(missingContext) &&
            missingContext.errors.error == ESP_ERR_INVALID_ARG &&
            fake::drive.transactions.empty(), "omitted invalid default address accepted");

  // Internal restore changes local identity only, including persisted pending ID.
  esp32m::Request restore(true);
  restore.payload["address"] = 7;
  restore.payload["pendingAddress"] = 9;
  esp32m::RequestContext restoreContext(restore);
  check(device.setDeviceConfig(restoreContext) && restoreContext.errors.empty() &&
            fake::drive.transactions.empty(), "internal restore accessed hardware");
  expectAddress(device, 7, 9);
  check(esp32m::modbus::Master::instance().starts == 0 &&
            esp32m::modbus::Master::instance().retunes == 0,
        "internal restore changed bus");
  esp32m::Request interactive;
  interactive.payload["address"] = 3;
  esp32m::RequestContext interactiveContext(interactive);
  check(!device.setDeviceConfig(interactiveContext) &&
            interactiveContext.errors.error != ESP_OK && fake::drive.transactions.empty(),
        "interactive change while pending was accepted");
  for (const char* payload : {R"({"address":7,"pendingAddress":0})",
      R"({"address":7,"pendingAddress":248})", R"({"address":7,"pendingAddress":7})",
      R"({"address":7,"pendingAddress":8.5})", R"({"address":7,"pendingAddress":true})",
      R"({"address":7,"pendingAddress":"9"})"}) {
    reset();
    TestCsf09 invalid;
    esp32m::Request request(true);
    check(!deserializeJson(request.payload, payload), "invalid restore test JSON");
    esp32m::RequestContext context(request);
    check(!invalid.setDeviceConfig(context) && context.errors.error == ESP_ERR_INVALID_ARG &&
              fake::drive.transactions.empty(), "invalid pending restore accepted");
    expectAddress(invalid, 1);
  }
  reset();
  TestCsf09 restored;
  check(restored.pollSensors(), "restore baseline");
  const auto calls = fake::drive.transactions.size();
  esp32m::Request identity(true);
  identity.payload["address"] = 7;
  identity.payload["baud"] = 9600;
  esp32m::RequestContext identityContext(identity);
  check(restored.setDeviceConfig(identityContext) && identityContext.errors.empty() &&
            fake::drive.transactions.size() == calls, "identity restore performed I/O");
  const auto unknown = state(restored);
  check(unknown[0].as<int64_t>() == -1 && unknown[2].isNull() && unknown[3].isNull(),
        "internal address change retained readings from old slave");
}
void documentedMeasurementProbes() {
  reset();
  TestCsf09 device;
  check(device.pollSensors(), "documented telemetry read failed");
  fake::drive.moisture = 987;
  fake::drive.temperature = 1000;
  const auto config = settings(device, false);
  check(config["communicationError"].as<int>() == ESP_OK,
        "config dialog failed despite working documented telemetry read");
  check(fake::drive.transactions.size() == 2 &&
            fake::drive.transactions[0].retry && !fake::drive.transactions[1].retry,
        "config verification must use one non-retried documented measurement read");
  for (const auto& transaction : fake::drive.transactions)
    check(transaction.command == 3 && transaction.reg == 0 && transaction.count == 2,
          "normal-reading/config-dialog regression used undocumented probe");
  const auto snapshot = state(device);
  expectValues(snapshot, 31.5, 15);
  check(snapshot[0].as<uint64_t>() == 1000 + fake::drive.requestMs &&
            device.sensors[0]->updates == 1 && device.sensors[1]->updates == 1,
        "configuration verification published/refreshed a telemetry sample");
}
void fixedBusGuards() {
  for (int fault = 0; fault < 4; ++fault) {
    reset();
    auto& master = esp32m::modbus::Master::instance();
    if (fault == 0) master.configured = false;
    if (fault == 1) master.settings.ser_opts.baudrate = 19200;
    if (fault == 2) master.settings.ser_opts.parity = 1;
    if (fault == 3) master.settings.ser_opts.mode = 1;
    TestCsf09 device;
    check(!device.initSensors() && !device.pollSensors(), "incompatible bus accepted");
    esp32m::Request request;
    request.payload["address"] = 2;
    esp32m::RequestContext context(request);
    check(!device.setDeviceConfig(context) && context.errors.error != ESP_OK,
          "address changed on incompatible bus");
    (void)settings(device, false);
    check(fake::drive.transactions.empty() && master.retunes == 0 && master.stops == 0 &&
              master.starts == 0, "fixed bus guard probed/retuned shared bus");
  }
  reset();
  auto& master = esp32m::modbus::Master::instance();
  master.running = false;
  TestCsf09 device;
  (void)settings(device, false);
  check(master.starts == 0 && fake::drive.transactions.empty(),
        "opening config started stopped shared bus");
}
void verifiedAddressChanges() {
  for (const auto ack : {ESP_OK, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE, ESP_FAIL}) {
    reset();
    TestCsf09 device;
    check(device.pollSensors(), "address migration baseline");
    fake::drive.ackError = ack;
    esp32m::Request request;
    request.payload["address"] = 9;
    request.payload["baud"] = 9600;
    esp32m::RequestContext context(request);
    check(device.setDeviceConfig(context) && context.errors.empty(),
          "verified new ID rejected after address write");
    expectAddress(device, 9);
    check(fake::drive.writes() == 1 && fake::drive.address == 9,
          "address migration did not use one write");
    const auto snapshot = state(device);
    check(snapshot[0].as<int64_t>() == -1 && snapshot[2].isNull() && snapshot[3].isNull(),
          "address migration retained old slave readings");
    bool sawOld = false, sawDestination = false, sawWrite = false, sawVerification = false;
    for (const auto& transaction : fake::drive.transactions) {
      if (transaction.command == 6) {
        check(sawOld && sawDestination && !sawWrite &&
                  !transaction.retry && transaction.count == 1 &&
                  transaction.reg == 0x000f && transaction.address == 1 &&
                  transaction.raw == 9, "write missing preflight or wrong ID/value");
        sawWrite = true;
      } else {
        check(transaction.reg == 0 && transaction.count == 2,
              "migration probe read undocumented registers");
        if (transaction.retry) continue;  // Initial ordinary telemetry read.
        if (transaction.address == 1) sawOld = true;
        if (transaction.address == 9) {
          if (sawWrite) sawVerification = true;
          else sawDestination = true;
        }
      }
    }
    check(sawWrite && sawVerification, "new address was not independently verified");
    check(device.pollSensors(), "telemetry failed at verified ID");
    const auto writes = fake::drive.writes();
    esp32m::Request noOp;
    noOp.payload["address"] = 9;
    esp32m::RequestContext noOpContext(noOp);
    check(!device.setDeviceConfig(noOpContext) && noOpContext.errors.empty() &&
              fake::drive.writes() == writes, "same-ID request wrote hardware");
    check(esp32m::modbus::Master::instance().retunes == 0 &&
              esp32m::modbus::Master::instance().stops == 0,
          "ID migration disturbed shared master");
  }
  reset();
  TestCsf09 ignored;
  fake::drive.ignoreWrite = true;
  esp32m::Request request;
  request.payload["address"] = 9;
  esp32m::RequestContext context(request);
  (void)ignored.setDeviceConfig(context);
  check(context.errors.error != ESP_OK && fake::drive.writes() == 1,
        "write ACK was accepted without verified changed ID");
  expectAddress(ignored, 1);
}
void addressPreflightRejections() {
  for (int fault = 0; fault < 11; ++fault) {
    reset();
    TestCsf09 device;
    if (fault == 0) fake::drive.measurementReplies[1] = {1001, 650};
    if (fault == 1) fake::drive.measurementErrors[1] = ESP_ERR_TIMEOUT;
    if (fault == 2) fake::drive.measurementReplies[9] = {315, 650};
    if (fault == 3) fake::drive.measurementReplies[9] = {0, 0};
    if (fault == 4) fake::drive.measurementReplies[9] = {1001, 650};
    if (fault == 5) fake::drive.measurementErrors[9] = ESP_ERR_INVALID_RESPONSE;
    if (fault == 6) fake::drive.measurementErrors[9] = ESP_FAIL;
    if (fault == 7) fake::drive.measurementReplies[9] = {1000, 1000};
    if (fault == 8) fake::drive.measurementReplies[9] = {315, 1001};
    if (fault == 9) fake::drive.measurementReplies[1] = {315, 1001};
    if (fault == 10) fake::drive.measurementErrors[9] = ESP_ERR_NOT_SUPPORTED;
    esp32m::Request request;
    request.payload["address"] = 9;
    esp32m::RequestContext context(request);
    check(!device.setDeviceConfig(context) && context.errors.error != ESP_OK &&
              fake::drive.writes() == 0, "unsafe ID preflight authorized write");
    expectAddress(device, 1);
  }
}
void pendingRecoveryAndRestart() {
  // Try each recovery entry point and both physically possible outcomes.
  for (int entry = 0; entry < 3; ++entry) {
    for (bool applied : {false, true}) {
      reset();
      TestCsf09 device;
      fake::drive.ignoreWrite = !applied;
      fake::drive.offlineAfterWrite = true;
      fake::drive.ackError = ESP_ERR_TIMEOUT;
      esp32m::Request request;
      request.payload["address"] = 9;
      esp32m::RequestContext context(request);
      (void)device.setDeviceConfig(context);
      check(context.errors.error != ESP_OK && fake::drive.writes() == 1,
            "uncertain address write not reported");
      expectAddress(device, 1, 9);
      const auto saved = settings(device);
      const auto pendingState = state(device);
      check(pendingState[4].as<bool>() && pendingState[5].as<int>() != ESP_OK,
            "uncertain outcome absent from state");
      check(!device.pollSensors() && fake::drive.writes() == 1,
            "unresolved poll retried address write");

      // Recreate the driver as after MCU restart, without any hardware access.
      TestCsf09 rebooted;
      esp32m::Request restore(true);
      restore.payload.set(saved);
      esp32m::RequestContext restoreContext(restore);
      const auto calls = fake::drive.transactions.size();
      check(rebooted.setDeviceConfig(restoreContext) && restoreContext.errors.empty() &&
                fake::drive.transactions.size() == calls, "pending restore performed I/O");
      expectAddress(rebooted, 1, 9);
      fake::drive.offline = false;
      const auto persisted = fake::persisted;
      if (entry == 0) check(rebooted.initSensors(), "init pending recovery failed");
      if (entry == 1) check(rebooted.pollSensors(), "poll pending recovery failed");
      if (entry == 2) (void)settings(rebooted, false);
      expectAddress(rebooted, applied ? 9 : 1);
      check(fake::drive.writes() == 1 && fake::persisted > persisted,
            "automatic recovery wrote/recovered identity was not persisted");
      check(esp32m::modbus::Master::instance().retunes == 0,
            "pending recovery retuned the shared bus");
    }
  }
  reset();
  TestCsf09 stopped;
  esp32m::Request restore(true);
  restore.payload["address"] = 1;
  restore.payload["pendingAddress"] = 9;
  esp32m::RequestContext context(restore);
  check(stopped.setDeviceConfig(context), "stopped recovery restore");
  esp32m::modbus::Master::instance().running = false;
  (void)settings(stopped, false);
  check(fake::drive.transactions.empty() &&
            esp32m::modbus::Master::instance().starts == 0,
        "config refresh restarted stopped pending bus");
  expectAddress(stopped, 1, 9);

  // Invalid transport/header replies cannot resolve the pending identity.
  reset();
  TestCsf09 mismatched;
  fake::drive.offlineAfterWrite = true;
  esp32m::Request change;
  change.payload["address"] = 9;
  esp32m::RequestContext changeContext(change);
  (void)mismatched.setDeviceConfig(changeContext);
  fake::drive.offline = false;
  fake::drive.measurementErrors[9] = ESP_ERR_INVALID_RESPONSE;
  check(!mismatched.pollSensors(), "invalid response resolved pending identity");
  expectAddress(mismatched, 1, 9);
  fake::drive.measurementErrors.clear();
  check(mismatched.pollSensors() && fake::drive.writes() == 1,
        "invalid-response recovery retried write/did not recover");
  expectAddress(mismatched, 9);

  // A syntactically successful read with impossible raw values is not a
  // valid verification either, including during saved-pending recovery.
  reset();
  TestCsf09 invalidValues;
  fake::drive.offlineAfterWrite = true;
  esp32m::Request invalidChange;
  invalidChange.payload["address"] = 9;
  esp32m::RequestContext invalidContext(invalidChange);
  (void)invalidValues.setDeviceConfig(invalidContext);
  fake::drive.offline = false;
  fake::drive.measurementReplies[9] = {315, 1001};
  check(!invalidValues.initSensors(), "out-of-range reply resolved pending ID");
  expectAddress(invalidValues, 1, 9);
  fake::drive.measurementReplies.clear();
  check(invalidValues.initSensors() && fake::drive.writes() == 1,
        "out-of-range pending recovery retried write/did not recover");
  expectAddress(invalidValues, 9);
}
void pollSkipsSlowConfiguration() {
  reset();
  TestCsf09 device;
  std::promise<void> blocked, release;
  auto blockedReady = blocked.get_future();
  auto releaseReady = release.get_future().share();
  bool entered = false;
  fake::drive.onRequest = [&](const fake::Transaction& transaction) {
    if (entered || transaction.command != 3 || transaction.reg != 0) return;
    entered = true;
    blocked.set_value();
    releaseReady.wait();
  };
  auto change = std::async(std::launch::async, [&] {
    esp32m::Request request;
    request.payload["address"] = 9;
    esp32m::RequestContext context(request);
    const auto changed = device.setDeviceConfig(context);
    return changed && context.errors.empty();
  });
  if (blockedReady.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
    release.set_value();
    (void)change.get();
    check(false, "configuration did not reach ID preflight");
  }
  const auto calls = fake::drive.transactions.size();
  auto poll = std::async(std::launch::async, [&] { return device.pollSensors(); });
  const auto quick = poll.wait_for(std::chrono::milliseconds(100)) ==
                     std::future_status::ready;
  const bool unchanged = fake::drive.transactions.size() == calls;
  release.set_value();
  check(change.get(), "slow ID change failed");
  check(poll.get() && quick && unchanged,
        "scheduler poll waited/accessed bus during slow configuration");
}
std::pair<bool, esp_err_t> connect(TestCsf09& device, unsigned address) {
  esp32m::Request request;
  request.payload["connectAddress"] = address;
  esp32m::RequestContext context(request);
  const auto changed = device.setDeviceConfig(context);
  return {changed, context.errors.error};
}
void connectValidationAndGuards() {
  for (const char* payload : {R"({"connectAddress":0})", R"({"connectAddress":248})",
      R"({"connectAddress":-1})", R"({"connectAddress":1.5})",
      R"({"connectAddress":true})", R"({"connectAddress":"1"})",
      R"({"connectAddress":null})", R"({"connectAddress":1,"address":1})",
      R"({"connectAddress":1,"baud":9600})",
      R"({"connectAddress":1,"pendingAddress":9})",
      R"({"connectAddress":1,"_disabled":false})",
      R"({"connectAddress":1,"unrecognized":0})"}) {
    reset();
    TestCsf09 device(9);
    esp32m::Request request;
    check(!deserializeJson(request.payload, payload), "invalid connect test JSON");
    esp32m::RequestContext context(request);
    check(!device.setDeviceConfig(context) && context.errors.error != ESP_OK &&
              fake::drive.transactions.empty(), "invalid/mixed connect request accessed bus");
    expectAddress(device, 9);
  }
  for (int mode = 0; mode < 3; ++mode) {
    reset();
    TestCsf09 device(9);
    esp32m::Request request(mode == 0);
    request.broadcast = mode == 1;
    device.disabledForTest = mode == 2;
    request.payload["connectAddress"] = 1;
    esp32m::RequestContext context(request);
    check(!device.setDeviceConfig(context) && context.errors.error != ESP_OK &&
              fake::drive.transactions.empty(), "unscoped/disabled/internal connect accepted");
    expectAddress(device, 9);
  }
  reset();
  TestCsf09 pending;
  esp32m::Request restore(true);
  restore.payload["address"] = 7;
  restore.payload["pendingAddress"] = 9;
  esp32m::RequestContext restoreContext(restore);
  check(pending.setDeviceConfig(restoreContext), "pending connect guard restore");
  const auto result = connect(pending, 1);
  check(!result.first && result.second != ESP_OK && fake::drive.transactions.empty(),
        "explicit connect bypassed unresolved old/new identity");
  expectAddress(pending, 7, 9);

  for (int fault = 0; fault < 5; ++fault) {
    reset();
    auto& master = esp32m::modbus::Master::instance();
    if (fault == 0) master.configured = false;
    if (fault == 1) master.settings.ser_opts.baudrate = 19200;
    if (fault == 2) master.settings.ser_opts.parity = 1;
    if (fault == 3) master.settings.ser_opts.mode = 1;
    if (fault == 4) master.running = false;
    TestCsf09 device(9);
    const auto attempt = connect(device, 1);
    check(!attempt.first && attempt.second != ESP_OK &&
              fake::drive.transactions.empty() && master.starts == 0 &&
              master.stops == 0 && master.retunes == 0,
          "connect started/retuned/probed incompatible or stopped shared bus");
    expectAddress(device, 9);
  }
  reset();
  fake::drive.address = 247;
  TestCsf09 lastId(9);
  const auto lastAttempt = connect(lastId, 247);
  check(lastAttempt.first && lastAttempt.second == ESP_OK &&
            fake::drive.transactions.size() == 1 && fake::drive.writes() == 0,
        "connect rejected valid upper unicast bound");
  expectAddress(lastId, 247);
}
void connectFailureAndSameAddress() {
  for (int fault = 0; fault < 5; ++fault) {
    reset();
    fake::drive.address = 9;
    TestCsf09 device(9);
    check(device.pollSensors(), "connect failure baseline");
    const auto stamp = fake::now.load();
    fake::drive.address = 1;
    if (fault == 0) fake::drive.measurementErrors[1] = ESP_ERR_TIMEOUT;
    if (fault == 1) fake::drive.measurementErrors[1] = ESP_ERR_INVALID_RESPONSE;
    if (fault == 2) fake::drive.measurementErrors[1] = ESP_FAIL;
    if (fault == 3) fake::drive.measurementReplies[1] = {1001, 650};
    if (fault == 4) fake::drive.measurementReplies[1] = {315, 1001};
    const auto calls = fake::drive.transactions.size();
    const auto attempt = connect(device, 1);
    check(!attempt.first && attempt.second != ESP_OK &&
              fake::drive.transactions.size() == calls + 1 &&
              fake::drive.writes() == 0, "failed connect changed identity or wrote hardware");
    expectAddress(device, 9);
    const auto snapshot = state(device);
    expectValues(snapshot, 31.5, 15);
    check(snapshot[0].as<uint64_t>() == stamp &&
              snapshot[5].as<int>() == attempt.second &&
              device.sensors[0]->updates == 1 && device.sensors[1]->updates == 1,
          "failed connect refreshed/invalidated previous sample");
  }
  reset();
  fake::drive.address = 9;
  TestCsf09 same(9);
  check(same.pollSensors(), "same address connect baseline");
  const auto stamp = fake::now.load();
  fake::drive.measurementErrors[9] = ESP_ERR_TIMEOUT;
  check(!same.pollSensors() && state(same)[5].as<int>() == ESP_ERR_TIMEOUT,
        "same address connect error baseline");
  fake::drive.measurementErrors.clear();
  fake::drive.moisture = 777;
  fake::drive.temperature = 1000;
  const auto calls = fake::drive.transactions.size();
  const auto attempt = connect(same, 9);
  check(!attempt.first && attempt.second == ESP_OK &&
            fake::drive.transactions.size() == calls + 1 && fake::drive.writes() == 0,
        "same address connect did not revalidate read-only");
  const auto snapshot = state(same);
  expectValues(snapshot, 31.5, 15);
  check(snapshot[0].as<uint64_t>() == stamp &&
            same.sensors[0]->updates == 1 && same.sensors[1]->updates == 1 &&
            snapshot[5].as<int>() == ESP_OK,
        "same address connect reset cached sample");
}
void replacementCommissioningCycle() {
  reset();
  TestCsf09 device;
  check(device.pollSensors(), "commissioning baseline");
  auto setPhysicalId = [&](unsigned address, unsigned expectedWrites) {
    esp32m::Request request;
    request.payload["address"] = address;
    esp32m::RequestContext context(request);
    check(device.setDeviceConfig(context) && context.errors.empty() &&
              fake::drive.writes() == expectedWrites && fake::drive.address == address,
          "commissioning physical ID change was not one-shot/verified");
    expectAddress(device, address);
    check(device.pollSensors(), "telemetry at commissioned physical ID");
  };
  setPhysicalId(9, 1);
  const auto saved9 = settings(device);
  check(saved9["connectAddress"].isUnbound(), "transient connect operation persisted");

  // Replace address-9 sensor with a factory address-1 unit. Merely restoring
  // saved settings or opening its dialog must continue to target saved ID 9.
  fake::drive.address = 1;
  TestCsf09 restored;
  esp32m::Request restore(true);
  restore.payload.set(saved9);
  esp32m::RequestContext restoreContext(restore);
  const auto restoreCalls = fake::drive.transactions.size();
  check(restored.setDeviceConfig(restoreContext) && restoreContext.errors.empty() &&
            fake::drive.transactions.size() == restoreCalls, "saved identity restore performed I/O");
  check(restored.initSensors() && !restored.pollSensors(),
        "saved identity automatically adopted replacement factory ID");
  const auto restoredConfig = settings(restored, false);
  check(restoredConfig["address"].as<unsigned>() == 9 &&
            restoredConfig["communicationError"].as<int>() != ESP_OK,
        "saved ID silently followed replacement sensor");
  for (size_t index = restoreCalls; index < fake::drive.transactions.size(); ++index)
    check(fake::drive.transactions[index].address == 9,
          "saved ID restoration/config refresh probed another ID automatically");

  auto adoptFactory = [&](TestCsf09& target) {
    const auto before = fake::drive.transactions.size();
    const auto writes = fake::drive.writes();
    const auto attempt = connect(target, 1);
    check(attempt.first && attempt.second == ESP_OK &&
              fake::drive.transactions.size() == before + 1 &&
              fake::drive.writes() == writes && fake::drive.address == 1,
          "factory connect changed physical ID or issued extra probes");
    const auto& probe = fake::drive.transactions.back();
    check(probe.address == 1 && probe.command == 3 && probe.reg == 0 &&
              probe.count == 2 && !probe.retry, "factory connect used wrong probe");
    expectAddress(target, 1);
    const auto unknown = state(target);
    check(unknown[0].as<int64_t>() == -1 && unknown[2].isNull() &&
              unknown[3].isNull() && unknown[5].as<int>() == ESP_OK,
          "factory connect retained previous sensor measurements/error");
    check(settings(target)["connectAddress"].isUnbound(),
          "connect operation leaked into saved config");
    check(target.pollSensors(), "factory sensor telemetry after explicit connect");
  };
  const auto oldConfig = settings(device, false);
  check(oldConfig["communicationError"].as<int>() != ESP_OK,
        "old-ID dialog unexpectedly found replacement");
  adoptFactory(device);
  setPhysicalId(10, 2);
  fake::drive.address = 1;
  adoptFactory(device);
  check(fake::drive.writes() == 2 &&
            esp32m::modbus::Master::instance().retunes == 0 &&
            esp32m::modbus::Master::instance().stops == 0,
        "replacement cycle added physical writes or disturbed shared master");
}

int main() {
  try {
    unknownAndMetadata();
    manualExampleAndEndpoints();
    documentedMeasurementProbes();
    rejectedSamplesPreserveCache();
    timestampAtBootAndLongUptime();
    sharedBusLifecycleAndAddressValidation();
    coherentConcurrentSnapshot();
    configurationValidationAndRestore();
    fixedBusGuards();
    verifiedAddressChanges();
    addressPreflightRejections();
    pendingRecoveryAndRestart();
    pollSkipsSlowConfiguration();
    connectValidationAndGuards();
    connectFailureAndSameAddress();
    replacementCommissioningCycle();
    std::cout << "CSF09 driver: 16 scenario groups passed\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
