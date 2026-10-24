#include <ArduinoJson.h>
#include "esp32m/ui/payload.hpp"
#include "esp32m/dev/bfs/parameters.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

int64_t esp_timer_get_time();
#include "serializer.inc"

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
  ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103,
  ESP_ERR_NOT_SUPPORTED = 0x106, ESP_ERR_TIMEOUT = 0x107,
  ESP_ERR_INVALID_RESPONSE = 0x108;
#define ESP_CHECK_RETURN(expr) do { const auto error = (expr); if (error != ESP_OK) return error; } while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expr) ((void)(expr))
template<class... T> void fakeLog(T...) {}
#define logW(...) fakeLog(__VA_ARGS__)
#define logI(...) fakeLog(__VA_ARGS__)
using TaskHandle_t = void*;
using TickType_t = uint32_t;
constexpr int pdPASS = 1, pdTRUE = 1, UART_PARITY_DISABLE = 0, MB_RTU = 0;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
namespace fake {
uint64_t now = 1000;
unsigned persisted = 0;
std::deque<std::function<void()>> tasks;
}
int64_t esp_timer_get_time() { return fake::now * 1000; }
int xTaskCreate(void (*fn)(void*), const char*, unsigned, void* arg, unsigned, TaskHandle_t* handle) {
  if (handle) *handle = reinterpret_cast<void*>(1);
  fake::tasks.push_back([=] { fn(arg); });
  return pdPASS;
}
void vTaskDelete(TaskHandle_t) {}
void vTaskDelay(TickType_t ticks) { fake::now += ticks; }
void esp_task_wdt_add(void*) {}
esp_err_t esp_task_wdt_status(void*) { return ESP_OK; }
void esp_task_wdt_reset() {}
void esp_task_wdt_delete(void*) {}
const char* esp_err_to_name(esp_err_t) { return "fake error"; }

namespace esp32m {
unsigned long millis() { return static_cast<uint32_t>(fake::now); }
void delay(unsigned ms) { fake::now += ms; }
struct Response;
struct Event {};
struct EventPeriodic { static bool is(Event&) { return true; } };
struct Request {
  std::string command;
  JsonDocument payload, response;
  bool internal = false, broadcast = false, responded = false;
  std::function<void()> onPublished;
  esp_err_t error = ESP_OK;
  explicit Request(const char* cmd = "", bool isInternal = false)
    : command(cmd), internal(isInternal) {}
  bool is(const char* value) const { return command == value; }
  bool isInternal() const { return internal; }
  bool isBroadcast() const { return broadcast; }
  JsonVariantConst data() const { return payload.as<JsonVariantConst>(); }
  void respondError(esp_err_t value, const char*) { respond(value); }
  void respond(esp_err_t value) { error = value; responded = true; }
  void respond(JsonVariantConst value, bool isError) {
    response.set(value); error = isError ? ESP_FAIL : ESP_OK; responded = true;
  }
  Response* makeResponse();
};
struct Response {
  Request* request;
  std::unique_ptr<JsonDocument> doc;
  bool error = false;
  explicit Response(Request* request) : request(request) {}
  void setData(JsonDocument* value) { doc.reset(value); error = false; }
  void setError(esp_err_t value) { doc = std::make_unique<JsonDocument>(); doc->set(value); error = true; }
  void publish() {
    if (doc) {
      const auto wire = ui::makeResponse(request->command.c_str(), "bfs", 1,
                                        doc->as<JsonVariantConst>(), error, false);
      // Include the actual platform's JSON copy and checked wire allocation:
      // the original 21 KB metadata reply crashed here on the ESP32-S3.
      assert(wire && wire.size() < 4096);
      assert(wire.capacity() < 8192);
      JsonDocument decoded;
      assert(!deserializeJson(decoded, wire.data(), wire.size()));
      request->response.set(decoded[error ? "error" : "data"]);
    }
    request->error = error ? request->response.as<int>() : ESP_OK;
    request->responded = true;
    if (request->onPublished) request->onPublished();
  }
};
Response* Request::makeResponse() { return new Response(this); }
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
  std::unique_ptr<JsonDocument> result;
  explicit RequestContext(Request& request) : request(request), data(request.data()) {}
  RequestContext(Request& request, JsonVariantConst data) : request(request), data(data) {}
};
struct AppObject {
  virtual ~AppObject() = default;
  virtual const char* name() const { return "fake"; }
  virtual bool handleRequest(Request&) { return false; }
  virtual void handleEvent(Event&) {}
};
struct Device : AppObject {
  enum Flags { HasSensors=1, CanDisable=2 };
  void setSensorsPollInterval(int) {}
  void init(int) {}
  bool disabledForTest = false;
  bool isDisabled() const { return disabledForTest; }
  std::recursive_mutex _deviceMutex;
  void resetSensors(bool) {}
  virtual bool initSensors() { return false; }
  virtual bool pollSensors() { return false; }
  virtual JsonDocument* getState(RequestContext&) { return nullptr; }
  virtual void setState(RequestContext&) {}
  virtual JsonDocument* getDeviceConfig(RequestContext&) { return nullptr; }
  virtual bool setDeviceConfig(RequestContext&) { return false; }
};
enum class StateClass { Measurement };
struct Sensor {
  const char* unit = nullptr;
  int precision = 0;
  StateClass stateClass = StateClass::Measurement;
  Sensor(Device*, const char*, const char*) {}
  void setTitle(const char*) {}
  void set(float) {}
  void resetState() {}
};
namespace config { struct Changed { static void publish(void*, bool = false) { ++fake::persisted; } }; }
struct Broadcast { static void publish(const char*, const char*) {} };
}

namespace fake {
struct Transaction { uint8_t address; int command; uint16_t reg, count, raw; bool retry; uint32_t baud; };
struct Drive {
  uint8_t address = 1;
  uint32_t baud = 9600;
  bool dropAck = false, ignoreWrite = false, offline = false, offlineAfterWrite = false;
  bool foreignTrafficBeforeRequest = false;
  std::map<uint16_t, esp_err_t> readErrors;
  std::map<uint8_t, esp_err_t> slaveErrors;
  std::vector<std::pair<uint8_t, uint32_t>> occupied;
  std::map<uint16_t, uint16_t> regs{
    {0x0004,160},{0x0005,5},{0x0006,90},{0x0008,2500},
    {0x000E,0},{0x000F,0},{0x0017,30},{0x0104,5000},{0x0105,54},{0x010A,5000},{0x010B,5000},
    {0x020A,1000},{0x020C,900},{0x020D,0x1101},
    {0x0306,1},{0x0307,1},{0x0308,2},{0x0309,0},{0x030C,1},
    {0x1000,0},{0x1001,3500},{0x1002,0},{0x1004,0},{0x1008,30},
    {0x1009,24},{0x100C,0x1000},{0x100D,247},{0x100E,2025},{0x100F,812},{0x2100,0}
  };
  std::vector<Transaction> transactions;
  size_t writes() const {
    return std::count_if(transactions.begin(), transactions.end(), [](auto t) { return t.command == 6; });
  }
} drive;
}
namespace esp32m::modbus {
enum Command { ReadHolding = 3, WriteRegister = 6 };
struct Serial { uint32_t baudrate = 9600; int parity = 0, mode = 0, data_bits = 8, stop_bits = 1, port = 2; uint32_t response_tout_ms = 450; };
struct Config { Serial ser_opts; };
struct Master {
  Config settings;
  bool running = true;
  uint32_t failNextBaud = 0;
  static Master& instance() { static Master master; return master; }
  const Config& config() const { return settings; }
  bool isConfigured() const { return true; }
  bool isRunning() const { return running; }
  esp_err_t start() { running = true; return ESP_OK; }
  esp_err_t setBaudrate(uint32_t value) {
    settings.ser_opts.baudrate = value;
    if (value == failNextBaud) { failNextBaud = 0; return ESP_FAIL; }
    return ESP_OK;
  }
  esp_err_t request(uint8_t addr, Command command, uint16_t reg, uint16_t count, void* buffer, bool retry = true, uint32_t quietBeforeMs = 0) {
    auto& drive = fake::drive;
    auto& value = *static_cast<uint16_t*>(buffer);
    drive.transactions.push_back({addr, command, reg, count,
      static_cast<uint16_t>(command == WriteRegister ? value : 0), retry, settings.ser_opts.baudrate});
    fake::now += 15;
    assert(count == 1);
    // A queued request can follow another slave immediately, regardless of
    // the BFS task's previous delay. Only the transport can reserve silence.
    if (drive.foreignTrafficBeforeRequest && quietBeforeMs < 12)
      return ESP_ERR_TIMEOUT;
    fake::now += quietBeforeMs;
    if (drive.slaveErrors.count(addr)) return drive.slaveErrors[addr];
    if (std::find(drive.occupied.begin(), drive.occupied.end(),
                  std::make_pair(addr, settings.ser_opts.baudrate)) != drive.occupied.end()) {
      assert(command == ReadHolding);
      value = reg == 0x0307 ? addr : 0;
      return ESP_OK;
    }
    if (drive.offline || addr != drive.address || settings.ser_opts.baudrate != drive.baud)
      return ESP_ERR_TIMEOUT;
    if (command == ReadHolding) {
      if (drive.readErrors.count(reg)) return drive.readErrors[reg];
      value = drive.regs[reg];
      return ESP_OK;
    }
    assert(command == WriteRegister && !retry);
    if (!drive.ignoreWrite) {
      drive.regs[reg] = value;
      if (reg == 0x0017) drive.regs[0x1008] = value;
      if (reg == 0x2000) {
        assert(value == 0x0012 || value == 0x0001);
        if (value == 0x0012) drive.regs[0x100C] |= 1;
        else { drive.regs[0x100C] &= ~1; drive.regs[0x1000] = 0; }
      }
      if (reg == 0x0306) drive.regs[0x100C] = (drive.regs[0x100C] & ~0x3000) | (value << 12);
      if (reg == 0x0307) drive.address = value;
      if (reg == 0x0308) {
        constexpr uint32_t rates[] = {2400,4800,9600,19200,38400,57600,115200};
        assert(value < 7); drive.baud = rates[value];
      }
    }
    if (drive.offlineAfterWrite) drive.offline = true;
    return drive.dropAck ? ESP_ERR_TIMEOUT : ESP_OK;
  }
};
}

#include "production.inc"

struct TestBfs : esp32m::dev::Bfs {
  using Bfs::Bfs;
  using Bfs::getState;
  using Bfs::setState;
  using Bfs::getDeviceConfig;
  using Bfs::setDeviceConfig;
  using Bfs::handleRequest;
  using Bfs::handleEvent;
  using Bfs::initSensors;
  using Bfs::pollSensors;
};
void reset() {
  assert(fake::tasks.empty());
  fake::drive = fake::Drive{};
  esp32m::modbus::Master::instance().settings.ser_opts.baudrate = 9600;
}
void drain() {
  while (!fake::tasks.empty()) {
    auto task = fake::tasks.front(); fake::tasks.pop_front(); task();
  }
}
JsonDocument state(TestBfs& bfs) {
  esp32m::Request request;
  esp32m::RequestContext context(request);
  std::unique_ptr<JsonDocument> result(bfs.getState(context));
  return *result;
}
JsonDocument config(TestBfs& bfs) {
  esp32m::Request request("", true);
  esp32m::RequestContext context(request);
  std::unique_ptr<JsonDocument> result(bfs.getDeviceConfig(context));
  return *result;
}
void send(TestBfs& bfs, esp32m::Request& request) {
  assert(bfs.handleRequest(request));
  drain();
  assert(request.responded);
}
esp32m::Request parameter(uint16_t address, uint16_t raw, uint16_t baseline) {
  esp32m::Request request("parameter-set");
  request.payload["address"] = address;
  request.payload["raw"] = raw;
  request.payload["expectedRaw"] = baseline;
  return request;
}
int main(int argc, char** argv) {
  reset();
  if (argc > 1 && std::string(argv[1]) == "--fixture") {
    TestBfs device;
    fake::drive.regs[0x0102] = fake::drive.regs[0x0103] = 75;
    fake::drive.regs[0x0202] = 800;
    fake::drive.regs[0x1012] = 5;
    fake::drive.regs[0x1013] = 4841;
    fake::drive.regs[0x1014] = 30;
    fake::drive.regs[0x1015] = 401;
    fake::drive.regs[0x1018] = 0x8201;
    fake::drive.regs[0x1019] = 247;
    assert(device.initSensors() && device.pollSensors());
    esp32m::Request parameters("parameters"), faults("faults");
    send(device, parameters);
    send(device, faults);
    JsonDocument fixture;
    fixture["state"].set(state(device));
    fixture["config"].set(config(device));
    fixture["parameters"].set(parameters.response);
    fixture["faults"].set(faults.response);
    serializeJson(fixture, std::cout);
    return 0;
  }
  TestBfs bfs;
  assert(std::string(bfs.name()) == "bfs");
  assert(state(bfs)["pressure"]["sampledAtMs"] == -1);
  assert(bfs.initSensors());
  assert(bfs.pollSensors());
  auto snapshot = state(bfs);
  assert(snapshot["pressure"]["valid"].as<bool>());
  const auto pressureSampleAt = snapshot["pressure"]["sampledAtMs"].as<uint64_t>();
  assert(pressureSampleAt > snapshot["frequency"]["sampledAtMs"].as<uint64_t>() &&
         snapshot["pressure"]["age"].isUnbound());
  assert(std::abs(snapshot["pressure"]["value"].as<float>() - 2.4f) < .001f);
  assert(std::abs(snapshot["frequencySetpoint"]["value"].as<float>() - 35.f) < .001f);
  assert(fake::drive.writes() == 0);
  const auto beforeState = fake::drive.transactions.size();
  fake::now += 37;
  (void)state(bfs);
  assert(state(bfs)["pressure"]["sampledAtMs"] == pressureSampleAt);
  assert(fake::drive.transactions.size() == beforeState);
  fake::drive.readErrors[0x1009] = ESP_ERR_TIMEOUT;
  assert(bfs.pollSensors());
  snapshot = state(bfs);
  assert(!snapshot["pressure"]["valid"].as<bool>());
  assert(snapshot["pressure"]["raw"].as<unsigned>() == 24);
  assert(snapshot["pressure"]["sampledAtMs"] == pressureSampleAt);
  assert(snapshot["frequency"]["valid"].as<bool>());
  fake::now += 6000;
  assert(!state(bfs)["frequency"]["valid"].as<bool>());
  fake::drive.readErrors.clear();
  fake::now = (uint64_t(1) << 33) + 500;
  assert(bfs.pollSensors());
  assert(state(bfs)["pressure"]["valid"].as<bool>());
  assert(state(bfs)["pressure"]["sampledAtMs"].as<uint64_t>() > (uint64_t(1) << 33));
  esp32m::Request mutation;
  mutation.payload["pressureSetpoint"] = 4;
  esp32m::RequestContext mutationContext(mutation);
  bfs.setState(mutationContext);
  assert(mutationContext.errors.error == ESP_ERR_NOT_SUPPORTED && fake::drive.writes() == 0);
  std::cout << "PASS telemetry: scaling, independent failures, stale values, 64-bit acquisition time, read-only state\n";

  reset();
  esp32m::Request faults("faults");
  send(bfs, faults);
  assert(faults.response["values"].size() == 10);
  assert(fake::drive.transactions.size() == 10);
  for (size_t i = 0; i < 10; ++i)
    assert(fake::drive.transactions[i].reg == 0x1010 + i);
  fake::drive.transactions.clear();
  esp32m::Request all("parameters");
  assert(bfs.handleRequest(all) && !all.responded);
  esp32m::Request busy("faults");
  assert(bfs.handleRequest(busy) && busy.error == ESP_ERR_INVALID_STATE);
  esp32m::Request stopDuringRead("run");
  stopDuringRead.payload["running"] = false;
  assert(bfs.handleRequest(stopDuringRead) && stopDuringRead.response["verified"].as<bool>());
  drain();
  assert(all.response["parameters"].size() == 80);
  for (auto entry : all.response["parameters"].as<JsonArray>()) {
    const auto address = entry["address"].as<unsigned>();
    assert(address < 0x0307 || address > 0x0309);
    assert(entry.size() == 2 && entry["raw"].is<uint16_t>());
  }
  fake::drive.transactions.clear();
  esp32m::Request one("parameters");
  one.payload["address"] = 0x0102;
  send(bfs, one);
  assert(one.response["parameters"].size() == 1 && fake::drive.transactions.size() == 1);
  fake::drive.transactions.clear();
  fake::drive.offline = true;
  esp32m::Request offline("parameters");
  send(bfs, offline);
  assert(offline.response["aborted"].as<bool>());
  assert(offline.response["abortReason"] == "failures");
  assert(offline.response["parameters"].size() == 80 && fake::drive.transactions.size() == 3);
  unsigned offlineIndex = 0;
  for (auto entry : offline.response["parameters"].as<JsonArray>()) {
    const bool skipped = offlineIndex++ >= 3;
    assert(entry.size() == (skipped ? 3 : 2));
    assert(entry["error"].as<int>() == (skipped ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT));
    assert(entry["skipped"].as<bool>() == skipped && entry["raw"].isNull());
  }
  fake::drive.offline = false;
  esp32m::Request excluded("parameters");
  excluded.payload["address"] = 0x0307;
  send(bfs, excluded);
  assert(excluded.error == ESP_ERR_INVALID_ARG);
  std::cout << "PASS dialogs: deferred demand reads, busy handling, STOP during read, single register refresh, bounded offline scan\n";

  reset();
  fake::drive.foreignTrafficBeforeRequest = true;
  esp32m::Request sharedFaults("faults"), sharedParameters("parameters");
  send(bfs, sharedFaults);
  send(bfs, sharedParameters);
  assert(!sharedFaults.response["aborted"].as<bool>());
  assert(!sharedParameters.response["aborted"].as<bool>());
  for (auto entry : sharedParameters.response["parameters"].as<JsonArray>())
    assert(entry["raw"].is<uint16_t>());
  assert(fake::drive.transactions.size() == 90 && fake::drive.writes() == 0);
  std::cout << "PASS shared bus: dialog reads reserve silence after foreign traffic\n";
  reset();

  // Exercise the maximum encoded value and error width, not only fixture zeros.
  for (const auto& p : esp32m::dev::bfs::Parameters)
    fake::drive.regs[p.address] = UINT16_MAX;
  esp32m::Request maximum("parameters");
  send(bfs, maximum);
  fake::drive.readErrors[0x0000] = INT32_MIN;
  esp32m::Request mixed("parameters");
  send(bfs, mixed);
  assert(mixed.response["parameters"][0]["error"].as<int>() == INT32_MIN);
  const auto wire = esp32m::ui::makeResponse("parameters", "bfs", INT32_MAX,
                                          maximum.response, false, false);
  std::cout << "PASS transport: real UI serializer, compact bulk/single/error replies, "
            << wire.size() << " bytes for 80 maximum-width values\n";

  reset();
  for (uint16_t raw = 0; raw <= 3; ++raw) {
    fake::drive.transactions.clear();
    fake::drive.regs[0x0204] = (raw + 1) % 4;
    auto phaseLoss = parameter(0x0204, raw, fake::drive.regs[0x0204]);
    send(bfs, phaseLoss);
    assert(phaseLoss.response["accepted"].as<bool>() &&
           phaseLoss.response["verified"].as<bool>());
    assert(fake::drive.writes() == 1 && fake::drive.regs[0x0204] == raw);
    const auto& last = fake::drive.transactions.back();
    assert(last.command == 3 && last.reg == 0x0204);
  }
  fake::drive.transactions.clear();
  for (const auto raw : {4, 65535}) {
    auto invalid = parameter(0x0204, raw, 3);
    send(bfs, invalid);
    assert(invalid.error != ESP_OK && fake::drive.writes() == 0);
  }
  fake::drive.regs[0x100C] = 1;
  auto phaseLossRunning = parameter(0x0204, 0, 3);
  send(bfs, phaseLossRunning);
  assert(phaseLossRunning.error != ESP_OK && fake::drive.writes() == 0);
  reset();
  for (const auto address : {0x000C,0x000D,0x0202,0x0208,0x0307,0x0308,0x0309,0x030C,0x0401,0xFFFF}) {
    auto request = parameter(address, 1, 0);
    send(bfs, request);
    assert(request.error != ESP_OK && fake::drive.writes() == 0);
  }
  auto stale = parameter(0x0102, 80, 75);
  fake::drive.regs[0x0102] = 70;
  send(bfs, stale);
  assert(stale.error == ESP_ERR_INVALID_STATE && fake::drive.writes() == 0);
  for (auto request : {parameter(0x0006,151,90), parameter(0x0008,5001,2500), parameter(0x020D,0x1102,0x1101)}) {
    send(bfs, request);
    assert(request.error != ESP_OK && fake::drive.writes() == 0);
  }
  auto noChange = parameter(0x0102,70,70);
  send(bfs, noChange);
  assert(noChange.response["verified"].as<bool>() && fake::drive.writes() == 0);
  fake::drive.dropAck = true;
  auto lostAck = parameter(0x0102,80,70);
  send(bfs, lostAck);
  assert(!lostAck.response["accepted"].as<bool>() && lostAck.response["verified"].as<bool>());
  assert(fake::drive.writes() == 1 && fake::drive.regs[0x0102] == 80);
  fake::drive.dropAck = false;
  fake::drive.ignoreWrite = true;
  auto ignored = parameter(0x0102,90,80);
  send(bfs, ignored);
  assert(ignored.response["accepted"].as<bool>() && !ignored.response["verified"].as<bool>());
  assert(fake::drive.writes() == 2);
  fake::drive.ignoreWrite = false;
  fake::drive.regs[0x100C] |= 1;
  auto whileRunning = parameter(0x0102,90,80);
  send(bfs, whileRunning);
  assert(whileRunning.error != ESP_OK && fake::drive.writes() == 2);
  fake::drive.regs[0x100C] = 0;
  fake::drive.regs[0x0306] = 0;
  fake::drive.transactions.clear();
  auto source = parameter(0x0306,1,0);
  send(bfs, source);
  assert(source.response["verified"].as<bool>() && fake::drive.writes() == 3);
  std::vector<uint16_t> writes;
  for (auto t : fake::drive.transactions) if (t.command == 6) writes.push_back(t.reg);
  assert((writes == std::vector<uint16_t>{0x2000,0x0306,0x2000}));
  std::cout << "PASS parameters: allowlist, bounds, stale baseline, no-op, lost ACK, false ACK, stopped guard, source-switch STOP sequence\n";

  reset();
  esp32m::Request unconfirmed("run");
  unconfirmed.payload["running"] = true;
  send(bfs, unconfirmed);
  assert(unconfirmed.error != ESP_OK && fake::drive.writes() == 0);
  esp32m::Request start("run");
  start.payload["running"] = true;
  start.payload["confirm"] = true;
  fake::drive.regs[0x0306] = 0;
  send(bfs, start);
  assert(start.error != ESP_OK && fake::drive.writes() == 0);
  fake::drive.regs[0x0306] = 1;
  fake::drive.regs[0x2100] = 24;
  send(bfs, start);
  assert(start.error != ESP_OK && fake::drive.writes() == 0);
  fake::drive.regs[0x2100] = 0;
  fake::drive.dropAck = true;
  send(bfs, start);
  assert(start.response["verified"].as<bool>() && !start.response["accepted"].as<bool>());
  assert(fake::drive.writes() == 1);
  for (const auto& t : fake::drive.transactions)
    if (t.command == 6) assert(t.reg == 0x2000 && t.raw == 0x0012);
  esp32m::Request stop("run");
  stop.payload["running"] = false;
  send(bfs, stop);
  assert(stop.response["verified"].as<bool>() && fake::drive.writes() == 2);
  assert(fake::drive.regs[0x2000] == 0x0001);
  esp32m::Request broadcast("run");
  broadcast.broadcast = true;
  broadcast.payload["running"] = true;
  broadcast.payload["confirm"] = true;
  send(bfs, broadcast);
  assert(broadcast.error != ESP_OK && fake::drive.writes() == 2);
  std::cout << "PASS control: explicit start confirmation, command-source/fault guards, one attempt, logical read-back, broadcast rejection\n";

  reset();
  for (const auto& tuple : {std::make_pair(uint8_t(2), uint32_t(9600)),
                           std::make_pair(uint8_t(1), uint32_t(19200))}) {
    TestBfs migrating;
    fake::drive.occupied = {tuple};
    esp32m::Request change;
    change.payload["address"] = tuple.first;
    change.payload["baud"] = tuple.second;
    esp32m::RequestContext context(change);
    assert(!migrating.setDeviceConfig(context) && !context.errors.empty());
    assert(fake::drive.writes() == 0);
    assert(config(migrating)["address"].as<int>() == 1);
    assert(esp32m::modbus::Master::instance().config().ser_opts.baudrate == 9600);
  }
  reset();
  {
    TestBfs migrating;
    auto& master = esp32m::modbus::Master::instance();
    master.failNextBaud = 19200;
    esp32m::Request change;
    change.payload["baud"] = 19200;
    esp32m::RequestContext context(change);
    assert(!migrating.setDeviceConfig(context) && !context.errors.empty());
    assert(master.config().ser_opts.baudrate == 9600 && fake::drive.writes() == 0);
  }
  reset();
  {
    TestBfs restored;
    esp32m::Request saved("", true);
    saved.payload["address"] = 7;
    saved.payload["baud"] = 19200;
    esp32m::RequestContext context(saved);
    assert(restored.setDeviceConfig(context) && context.errors.empty());
    assert(config(restored)["address"].as<int>() == 7);
    assert(fake::drive.writes() == 0);
  }
  reset();
  {
    TestBfs migrating;
    fake::drive.dropAck = true;
    esp32m::Request change;
    change.payload["address"] = 2;
    change.payload["baud"] = 19200;
    esp32m::RequestContext context(change);
    assert(migrating.setDeviceConfig(context) && context.errors.empty());
    auto verified = config(migrating);
    assert(verified["address"].as<int>() == 2 && verified["baud"].as<int>() == 19200);
    assert(verified["parameterCache"]["values"].size() == 0);
    assert(fake::drive.writes() == 2);
    assert(migrating.pollSensors());
  }
  reset();
  {
    TestBfs migrating;
    fake::drive.offlineAfterWrite = true;
    esp32m::Request change;
    change.payload["address"] = 3;
    esp32m::RequestContext context(change);
    assert(migrating.setDeviceConfig(context) && !context.errors.empty());
    auto pending = config(migrating);
    assert(pending["address"].as<int>() == 1 && pending["pendingAddress"].as<int>() == 3);
    assert(fake::drive.writes() == 1);
    TestBfs rebooted;
    esp32m::Request saved("", true);
    saved.payload.set(pending);
    esp32m::RequestContext restoreContext(saved);
    assert(rebooted.setDeviceConfig(restoreContext) && restoreContext.errors.empty());
    assert(fake::drive.writes() == 1);
    fake::drive.offline = fake::drive.offlineAfterWrite = false;
    assert(rebooted.initSensors());
    assert(config(rebooted)["address"].as<int>() == 3 && fake::drive.writes() == 1);
  }
  std::cout << "PASS communication: collisions/startup failure, restore without writes, lost-ACK migration, persisted uncertainty, read-only recovery after restart\n";

  reset();
  {
    TestBfs diagnosing;
    const auto before = fake::drive.transactions.size();
    (void)config(diagnosing);
    assert(fake::drive.transactions.size() == before);
    esp32m::Request readOnly("config-get");
    readOnly.payload["probeAddress"] = 5;
    esp32m::RequestContext context(readOnly);
    std::unique_ptr<JsonDocument> result(diagnosing.getDeviceConfig(context));
    assert((*result)["communication"]["addressRaw"].as<int>() == 1);
    assert((*result)["communication"]["baudRaw"].as<int>() == 2);
    assert((*result)["targetCommunication"]["error"].as<int>() == ESP_ERR_TIMEOUT);
    assert(fake::drive.transactions.size() == before + 3 && fake::drive.writes() == 0);
    readOnly.payload["probeAddress"] = 0;
    esp32m::RequestContext invalid(readOnly);
    result.reset(diagnosing.getDeviceConfig(invalid));
    assert((*result)["targetCommunication"]["error"].as<int>() == ESP_ERR_INVALID_ARG);
    assert(fake::drive.writes() == 0);
    auto& master = esp32m::modbus::Master::instance();
    master.settings.ser_opts.baudrate = 19200;
    const auto requests = fake::drive.transactions.size();
    result.reset(diagnosing.getDeviceConfig(invalid));
    assert((*result)["communication"]["error"].as<int>() == ESP_ERR_INVALID_STATE);
    assert(master.config().ser_opts.baudrate == 19200);
    assert(fake::drive.transactions.size() == requests);
  }
  for (const auto mode : {0, 1, 2, 3, 4}) {
    reset();
    TestBfs diagnosing;
    esp32m::Request change;
    change.payload["address"] = 5;
    if (mode == 0) fake::drive.regs[0x0307] = 9;
    if (mode == 1) fake::drive.slaveErrors[5] = ESP_ERR_INVALID_RESPONSE;
    if (mode == 2) fake::drive.ignoreWrite = true;
    if (mode == 3) fake::drive.regs[0x100C] |= 1;
    esp32m::RequestContext context(change);
    const bool changed = diagnosing.setDeviceConfig(context);
    assert(changed == (mode == 2 || mode == 4));
    assert(fake::drive.writes() == (changed ? 1 : 0));
    assert(context.errors.empty() == (mode == 4));
    if (mode == 0) assert(context.errors.message.find("Current F3-07/F3-08") != std::string::npos);
    if (mode == 1) assert(context.errors.message.find("Destination probe failed") != std::string::npos);
    if (mode == 2) assert(context.errors.message.find("previous settings still respond") != std::string::npos);
    if (mode == 3) assert(context.errors.message.find("STOP and 0 Hz") != std::string::npos);
    esp32m::Request readOnly("config-get");
    esp32m::RequestContext inspect(readOnly);
    std::unique_ptr<JsonDocument> result(diagnosing.getDeviceConfig(inspect));
    auto details = (*result)["lastCommunicationChange"];
    assert(details["writeAttempted"].as<bool>() == changed);
    if (mode == 0) assert(details["before"]["addressRaw"].as<int>() == 9);
    if (mode == 2) {
      assert(details["writeError"].as<int>() == ESP_OK);
      assert(details["after"]["error"].as<int>() == ESP_ERR_TIMEOUT);
      assert(details["previous"]["error"].as<int>() == ESP_OK);
    }
    if (mode == 4) assert((*result)["communication"]["addressRaw"].as<int>() == 5);
    assert(config(diagnosing)["lastCommunicationChange"].isNull());
    assert(fake::drive.writes() == (changed ? 1 : 0));
  }
  std::cout << "PASS communication diagnostics: read-only probes, no persistence I/O, stage-specific errors, write-attempt/ACK/read-back evidence\n";
  reset();
  {
    TestBfs device;
    esp32m::Request job("parameters");
    assert(device.handleRequest(job));
    device.disabledForTest = true;
    drain();
    assert(job.response["aborted"] == true && fake::drive.transactions.empty());
    assert(job.response["abortReason"] == "disabled");
    for (auto entry : job.response["parameters"].as<JsonArray>())
      assert(entry["skipped"] == true && entry["raw"].isNull());
    esp32m::Request request("config-get");
    esp32m::RequestContext context(request);
    std::unique_ptr<JsonDocument> cached(device.getDeviceConfig(context));
    assert(fake::drive.transactions.empty() && (*cached)["communication"].isUnbound());
  }
  std::cout << "PASS disabled settings and queued dialog: zero bus attempts\n";

  reset();
  {
    TestBfs device;
    fake::drive.regs[0x100C] = 0x1001;
    auto pressure = parameter(0x0017, 35, 30);
    send(device, pressure);
    assert(pressure.response["verified"] == true && fake::drive.writes() == 1);
    assert(fake::drive.regs[0x1008] == 35 && fake::drive.regs[0x100C] == 0x1001);
    for (auto t : fake::drive.transactions) if (t.command == 6) assert(t.reg == 0x0017);
    auto low = parameter(0x0017, 4, 35), high = parameter(0x0017, 91, 35);
    send(device, low); send(device, high);
    assert(low.error && high.error && fake::drive.writes() == 1);
    fake::drive.regs[0x000E] = 1;
    auto wrongMode = parameter(0x0017, 34, 35); send(device, wrongMode);
    assert(wrongMode.error && fake::drive.writes() == 1);
    fake::drive.regs[0x000E] = 0;
    fake::drive.regs[0x2100] = 5;
    auto faulted = parameter(0x0017, 34, 35); send(device, faulted);
    assert(faulted.error && fake::drive.writes() == 1);
    fake::drive.regs[0x2100] = 0;
    fake::drive.dropAck = true;
    auto uncertain = parameter(0x0017, 34, 35); send(device, uncertain);
    assert(uncertain.response["verified"] == true && uncertain.response["accepted"] == false);
    assert(fake::drive.writes() == 2);
    fake::drive.readErrors[0x1008] = ESP_ERR_TIMEOUT;
    auto unreadMonitor = parameter(0x0017, 35, 34); send(device, unreadMonitor);
    assert(unreadMonitor.response["verified"] == false && fake::drive.writes() == 3);
  }
  std::cout << "PASS pressure target: RUN-enabled writes, live limits/mode/fault, monitor verification, no retries\n";

  reset();
  {
    TestBfs device;
    esp32m::Request empty("parameter-cache"); send(device, empty);
    assert(empty.response["cached"] == true && empty.response["parameters"].size() == 0);
    assert(fake::drive.transactions.empty());
    const auto persisted = fake::persisted;
    esp32m::Request one("parameters"); one.payload["address"] = 0x0017; send(device, one);
    assert(fake::persisted == persisted);
    fake::now += 20000;
    esp32m::Event event; device.handleEvent(event);
    assert(fake::persisted == persisted);
    fake::now += 10001; device.handleEvent(event);
    assert(fake::persisted == persisted + 1);
    send(device, one); fake::now += 30001; device.handleEvent(event);
    assert(fake::persisted == persisted + 1); // Unchanged reads do not wear flash.
    const auto saved = config(device);
    const auto count = fake::drive.transactions.size();
    TestBfs restored;
    esp32m::Request load("", true); load.payload.set(saved);
    esp32m::RequestContext ctx(load); assert(restored.setDeviceConfig(ctx));
    assert(fake::drive.transactions.size() == count); // Boot never applies cache.
    restored.disabledForTest = true;
    esp32m::Request cache("parameter-cache"); send(restored, cache);
    assert(cache.response["parameters"][0]["raw"] == 30);
    assert(fake::drive.transactions.size() == count && fake::drive.writes() == 0);
    restored.disabledForTest = false;
    fake::drive.readErrors[0x0017] = ESP_ERR_TIMEOUT;
    esp32m::Request failed("parameters"); failed.payload["address"] = 0x0017; send(restored, failed);
    send(restored, cache);
    assert(cache.response["parameters"][0]["raw"] == 30); // Preserve last known value.
    auto stale = parameter(0x0017, 35, 30);
    fake::drive.readErrors.clear(); fake::drive.regs[0x0017] = 34;
    send(restored, stale);
    assert(stale.error == ESP_ERR_INVALID_STATE && fake::drive.writes() == 0);
    esp32m::Request forged; forged.payload["parameterCache"].set(saved["parameterCache"]);
    esp32m::RequestContext forgedCtx(forged);
    assert(!restored.setDeviceConfig(forgedCtx) && !forgedCtx.errors.empty());
    load.payload["parameterCache"]["address"] = 2;
    esp32m::RequestContext mismatch(load); assert(restored.setDeviceConfig(mismatch));
    send(restored, cache); assert(cache.response["parameters"].size() == 0);

    for (const auto& p : esp32m::dev::bfs::Parameters) fake::drive.regs[p.address] = 65535;
    esp32m::Request all("parameters"); send(restored, all);
    const auto populated = config(restored);
    const auto beforeCache = fake::drive.transactions.size();
    send(restored, cache);
    assert(cache.response["parameters"].size() == esp32m::dev::bfs::parametersCount);
    const auto wire = esp32m::ui::makeResponse("parameter-cache", "bfs", 1,
        cache.response.as<JsonVariantConst>(), false, false);
    assert(wire.size() < 4096 && wire.capacity() < 8192);
    assert(measureJson(populated) < 4096);
    load.payload.set(populated);
    esp32m::RequestContext restoreAll(load); assert(restored.setDeviceConfig(restoreAll));
    send(restored, cache);
    assert(cache.response["parameters"].size() == esp32m::dev::bfs::parametersCount);
    assert(fake::drive.transactions.size() == beforeCache && fake::drive.writes() == 0);
  }
  std::cout << "PASS parameter cache: no-I/O reads, deferred changed-only persistence, reboot/disabled cache, stale guards, identity and injection\n";

  reset();
  {
    TestBfs device;
    esp32m::Request first("parameters"), next("parameters");
    first.payload["address"] = 0x0017;
    next.payload["address"] = 0x0102;
    first.onPublished = [&] { assert(device.handleRequest(next)); };
    send(device, first);
    assert(next.responded && next.error == ESP_OK);
    assert(next.response["parameters"][0]["address"] == 0x0102);
    assert(fake::drive.transactions.size() == 2);
  }
  std::cout << "PASS sequential refresh: next read accepted as soon as previous response is published\n";

}
