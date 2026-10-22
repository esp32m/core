#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101,
  ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103,
  ESP_ERR_NOT_SUPPORTED = 0x106, ESP_ERR_TIMEOUT = 0x107,
  ESP_ERR_INVALID_RESPONSE = 0x108;
#define ESP_CHECK_RETURN(expr) do { const auto error = (expr); if (error != ESP_OK) return error; } while (0)
#define ESP_CHECK_RETURN_BOOL(expr) do { if ((expr) != ESP_OK) return false; } while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expr) ((void)(expr))
template<class... T> void fakeLog(T...) {}
#define logW(...) fakeLog(__VA_ARGS__)
#define logI(...) fakeLog(__VA_ARGS__)
constexpr int UART_PARITY_DISABLE = 0, MB_RTU = 0, MB_ADDRESS_MAX = 247;
namespace fake {
uint64_t now = 1000;
unsigned persisted = 0;
bool sensorValid = false, sensorValue = false;
bool analogValid = false;
float analogValue = 0;
}
int64_t esp_timer_get_time() { return fake::now * 1000; }
const char* esp_err_to_name(esp_err_t) { return "fake error"; }
esp_err_t esp_task_wdt_status(void*) { return ESP_OK; }
void esp_task_wdt_reset() {}

namespace esp32m {
unsigned long millis() { return static_cast<uint32_t>(fake::now); }
void delay(unsigned ms) { fake::now += ms; }
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
  explicit RequestContext(Request& request) : request(request), data(request.data()) {}
};
struct Device {
  enum Flags { HasSensors=1, CanDisable=2 };
  virtual ~Device() = default;
  virtual const char* name() const { return "fake"; }
  void setSensorsPollInterval(int) {}
  void init(int) {}
  bool disabledForTest = false;
  bool isDisabled() const { return disabledForTest; }
  std::recursive_mutex _deviceMutex;
  void resetSensors(bool = false) {}
  virtual bool initSensors() { return false; }
  virtual bool pollSensors() { return false; }
  virtual JsonDocument* getState(RequestContext&) { return nullptr; }
  virtual JsonDocument* getDeviceConfig(RequestContext&) { return nullptr; }
  virtual bool setDeviceConfig(RequestContext&) { return false; }
};
struct BinarySensor {
  BinarySensor(Device*, const char*, const char*) { fake::sensorValid = false; }
  void setTitle(const char*) {}
  void set(bool value) { fake::sensorValid = true; fake::sensorValue = value; }
  void resetState() { fake::sensorValid = false; }
};
enum class StateClass { Measurement };
struct Sensor {
  int precision = 0;
  StateClass stateClass = StateClass::Measurement;
  Sensor(Device*, const char*, const char*) { fake::analogValid = false; }
  void setTitle(const char*) {}
  void set(float value) { fake::analogValid = true; fake::analogValue = value; }
  void resetState() { fake::analogValid = false; }
};
namespace config { struct Changed { static void publish(void*, bool = false) { ++fake::persisted; } }; }
}

namespace fake {
constexpr uint16_t Alarm = 0, Analog = 1, Sensitivity = 2, Address = 3,
                   Baud = 4, AlarmDelay = 5;
constexpr std::array<uint32_t, 6> Rates{2400, 4800, 9600, 19200, 38400, 115200};
struct Transaction {
  uint8_t address;
  int command;
  uint16_t reg, count, raw;
  bool retry;
  uint32_t baud;
  uint32_t quietBeforeMs;
};
struct Drive {
  uint8_t address = 5;
  uint32_t baud = 9600;
  esp_err_t ackError = ESP_OK;
  bool offline = false;
  bool needsQuiet = false;
  int offlineAfterWrite = -1, ignoreWrite = -1;
  std::map<uint16_t, esp_err_t> readErrors;
  std::map<uint8_t, esp_err_t> slaveErrors;
  std::vector<std::pair<uint8_t, uint32_t>> occupied;
  std::map<uint16_t, uint16_t> regs{{Alarm, 0}, {Analog, 347},
    {Sensitivity, 300}, {Address, 5}, {Baud, 3}, {AlarmDelay, 2}};
  std::vector<Transaction> transactions;
  std::function<void()> onRequest;
  size_t writes() const {
    return std::count_if(transactions.begin(), transactions.end(),
                         [](auto t) { return t.command == 6; });
  }
} drive;
}
namespace esp32m::modbus {
enum Command { ReadHolding = 3, WriteRegister = 6 };
struct Serial {
  uint32_t baudrate = 9600;
  int parity = UART_PARITY_DISABLE, mode = MB_RTU, data_bits = 8,
      stop_bits = 1, port = 2;
  uint32_t response_tout_ms = 450;
};
struct Config { Serial ser_opts; };
struct Master {
  Config settings;
  bool running = true, configured = true;
  uint32_t failBaud = 0;
  bool failBeforeBaudApply = false;
  unsigned failBaudAt = 1, baudAttempts = 0;
  esp_err_t baudError = ESP_FAIL, startError = ESP_OK;
  uint32_t failStartBaud = 0;
  unsigned failStartAt = 1, startAttempts = 0;
  static Master& instance() { static Master master; return master; }
  const Config& config() const { return settings; }
  bool isConfigured() const { return configured; }
  bool isRunning() const { return running; }
  esp_err_t start() {
    if ((!failStartBaud || settings.ser_opts.baudrate == failStartBaud) &&
        ++startAttempts == failStartAt && startError != ESP_OK)
      return startError;
    running = true;
    return ESP_OK;
  }
  esp_err_t stop() { running = false; return ESP_OK; }
  esp_err_t setBaudrate(uint32_t value) {
    if (settings.ser_opts.baudrate == value) return ESP_OK;
    const bool fail = value == failBaud && ++baudAttempts == failBaudAt;
    if (fail && failBeforeBaudApply) return baudError;
    const bool wasRunning = running;
    running = false;
    settings.ser_opts.baudrate = value;
    if (fail) {
      running = false;
      return baudError;
    }
    return wasRunning ? start() : ESP_OK;
  }
  esp_err_t request(uint8_t addr, Command command, uint16_t reg,
                    uint16_t count, void* buffer, bool retry = true,
                    uint32_t quietBeforeMs = 0) {
    auto& drive = fake::drive;
    if (drive.onRequest) {
      auto callback = std::move(drive.onRequest);
      drive.onRequest = nullptr;
      callback();
    }
    auto& value = *static_cast<uint16_t*>(buffer);
    drive.transactions.push_back({addr, command, reg, count,
      static_cast<uint16_t>(command == WriteRegister ? value : 0), retry,
      settings.ser_opts.baudrate, quietBeforeMs});
    fake::now += quietBeforeMs;
    fake::now += 15;
    assert(count == 1);  // The vendor documents only single-register reads.
    assert(reg <= fake::AlarmDelay);
    // Model a slave that needs receiver recovery after other bus traffic.
    // Transport tests separately check that this interval holds the UART lock.
    if (drive.needsQuiet && quietBeforeMs < 100) return ESP_ERR_TIMEOUT;
    if (drive.slaveErrors.count(addr)) return drive.slaveErrors[addr];
    if (std::find(drive.occupied.begin(), drive.occupied.end(),
                  std::make_pair(addr, settings.ser_opts.baudrate)) != drive.occupied.end()) {
      assert(command == ReadHolding);
      value = reg == fake::Address ? addr : 0;
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
    assert(reg >= fake::Sensitivity);  // No writes to measured status.
    if (drive.ignoreWrite != reg) {
      drive.regs[reg] = value;
      if (reg == fake::Address) drive.address = value;
      if (reg == fake::Baud) {
        assert(value >= 1 && value <= fake::Rates.size());
        drive.baud = fake::Rates[value - 1];
      }
    }
    if (drive.offlineAfterWrite == reg) drive.offline = true;
    return drive.ackError;
  }
};
}

#include "production.inc"

struct TestHsmWt10 : esp32m::dev::HsmWt10 {
  using HsmWt10::HsmWt10;
  using HsmWt10::getState;
  using HsmWt10::getDeviceConfig;
  using HsmWt10::setDeviceConfig;
  using HsmWt10::initSensors;
  using HsmWt10::pollSensors;
};
void reset() {
  fake::drive = fake::Drive{};
  esp32m::modbus::Master::instance() = esp32m::modbus::Master{};
  fake::sensorValid = false;
  fake::sensorValue = false;
  fake::analogValid = false;
}
JsonDocument state(TestHsmWt10& device) {
  esp32m::Request request;
  esp32m::RequestContext context(request);
  std::unique_ptr<JsonDocument> result(device.getState(context));
  return *result;
}
JsonDocument config(TestHsmWt10& device, bool internal = true) {
  esp32m::Request request(internal);
  esp32m::RequestContext context(request);
  std::unique_ptr<JsonDocument> result(device.getDeviceConfig(context));
  return *result;
}
void assertTuple(TestHsmWt10& device, uint8_t address, uint32_t baud) {
  const auto cfg = config(device);
  assert(cfg["address"].as<unsigned>() == address);
  assert(cfg["baud"].as<unsigned>() == baud);
}

int main() {
  reset();
  {
    fake::drive.needsQuiet = true;
    TestHsmWt10 device;
    assert(device.initSensors());
    assert(device.pollSensors());
    assert(state(device)["valid"].as<bool>());
    auto cfg = config(device, false);
    assert(cfg["configReadError"].as<int>() == ESP_OK);
    assert(cfg["sensitivity"].as<unsigned>() == 300);
    for (const auto& transaction : fake::drive.transactions) {
      assert(transaction.quietBeforeMs == 100);
      assert(!transaction.retry);
    }
  }
  std::cout << "PASS shared bus: telemetry and settings request a reserved quiet interval without retransmission\n";
  reset();
  {
    TestHsmWt10 device;
    assert(std::string(device.name()) == "HSM-WT10");
    auto snapshot = state(device);
    assert(!snapshot["valid"].as<bool>());
    assert(snapshot["sampledAtMs"] == -1 && snapshot["age"].isUnbound());
    assert(snapshot["alarm"].isNull() && snapshot["analog"].isNull());
    assertTuple(device, 5, 9600);
    assert(device.initSensors() && device.pollSensors());
    snapshot = state(device);
    assert(snapshot["valid"].as<bool>() && !snapshot["alarm"].as<bool>());
    const auto firstSampleAt = snapshot["sampledAtMs"].as<uint64_t>();
    assert(firstSampleAt == fake::now);
    fake::now += 37;
    assert(state(device)["sampledAtMs"] == firstSampleAt);
    assert(snapshot["analog"].as<unsigned>() == 347);
    assert(fake::sensorValid && !fake::sensorValue);
    assert(fake::analogValid && fake::analogValue == 347);
    fake::drive.regs[fake::Alarm] = 1;
    fake::drive.regs[fake::Analog] = 65535;
    assert(device.pollSensors());
    snapshot = state(device);
    assert(snapshot["alarm"].as<bool>() && snapshot["analog"].as<unsigned>() == 65535);
    assert(fake::sensorValid && fake::sensorValue);
    assert(device.isValid() && device.value());
    fake::now += 5001;
    assert(!device.isValid());
    assert(!state(device)["valid"].as<bool>() && state(device)["alarm"].isNull());
    fake::now = (uint64_t(1) << 33) + 500;
    assert(device.pollSensors() && device.isValid());
    assert(state(device)["sampledAtMs"].as<uint64_t>() == fake::now);
    fake::drive.readErrors[fake::Analog] = ESP_ERR_TIMEOUT;
    assert(!device.pollSensors() && !fake::sensorValid && !fake::analogValid);
    fake::drive.readErrors.clear();
    assert(device.pollSensors());
    const auto reads = fake::drive.transactions.size();
    (void)state(device);
    (void)config(device);
    assert(fake::drive.transactions.size() == reads && fake::drive.writes() == 0);
    fake::drive.regs[fake::Alarm] = 2;
    assert(!device.pollSensors());
    snapshot = state(device);
    assert(!snapshot["valid"].as<bool>() && snapshot["alarm"].isNull());
    assert(!fake::sensorValid && !fake::analogValid);
    fake::drive.regs[fake::Alarm] = 0;
    assert(device.pollSensors());
    fake::drive.offline = true;
    assert(!device.pollSensors());
    snapshot = state(device);
    assert(!snapshot["valid"].as<bool>() && snapshot["alarm"].isNull());
    assert(!fake::sensorValid && snapshot["lastError"].as<int>() != ESP_OK);
  }
  std::cout << "PASS telemetry: normal/alarm/raw analog, freshness/64-bit acquisition time, read-only snapshots, invalid/failed reads clear both sensors\n";

  for (const auto* json : {"{\"address\":0}", "{\"address\":248}",
                          "{\"address\":253}", "{\"address\":2.5}",
                          "{\"address\":true}", "{\"address\":\"6\"}",
                          "{\"baud\":57600}", "{\"baud\":-1}",
                          "{\"baud\":9600.5}", "{\"sensitivity\":199}",
                          "{\"sensitivity\":501}", "{\"sensitivity\":null}",
                          "{\"alarmDelay\":-1}", "{\"alarmDelay\":65536}",
                          "{\"alarmDelay\":false}",
                          "{\"address\":6,\"alarmDelay\":65536}"}) {
    reset();
    TestHsmWt10 device;
    esp32m::Request request;
    assert(!deserializeJson(request.payload, json));
    esp32m::RequestContext context(request);
    assert(!device.setDeviceConfig(context));
    assert(context.errors.error == ESP_ERR_INVALID_ARG);
    assert(fake::drive.transactions.empty());
  }
  reset();
  {
    TestHsmWt10 device;
    esp32m::Request request(true);
    request.payload["address"] = 7;
    request.payload["baud"] = 19200;
    request.payload["sensitivity"] = 400;
    request.payload["alarmDelay"] = 4;
    esp32m::RequestContext context(request);
    assert(device.setDeviceConfig(context) && context.errors.empty());
    assertTuple(device, 7, 19200);
    assert(fake::drive.writes() == 0);
    assert(config(device)["sensitivity"].isNull());
  }
  std::cout << "PASS validation: strict types/ranges before I/O, full-request validation, internal restore without hardware writes\n";

  for (const auto ackError : {ESP_OK, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE}) {
    reset();
    TestHsmWt10 device;
    fake::drive.ackError = ackError;
    esp32m::Request request;
    request.payload["address"] = 9;
    esp32m::RequestContext context(request);
    assert(device.setDeviceConfig(context) && context.errors.empty());
    assertTuple(device, 9, 9600);
    assert(fake::drive.writes() == 1);
    auto write = std::find_if(fake::drive.transactions.begin(), fake::drive.transactions.end(),
                              [](auto t) { return t.command == 6; });
    assert(write->address == 5 && write->reg == fake::Address && write->raw == 9);
    assert(std::any_of(write + 1, fake::drive.transactions.end(),
                      [](auto t) { return t.command == 3 && t.address == 9 && t.reg == fake::Address; }));
    assert(device.pollSensors());
  }
  for (size_t code = 0; code < fake::Rates.size(); ++code) {
    reset();
    TestHsmWt10 device;
    fake::drive.ackError = ESP_ERR_TIMEOUT;
    esp32m::Request request;
    request.payload["baud"] = fake::Rates[code];
    esp32m::RequestContext context(request);
    const auto changed = device.setDeviceConfig(context);
    assert(context.errors.empty());
    assert(changed == (fake::Rates[code] != 9600));
    assertTuple(device, 5, fake::Rates[code]);
    assert(fake::drive.writes() == (fake::Rates[code] == 9600 ? 0u : 1u));
    for (const auto& t : fake::drive.transactions)
      if (t.command == 6) assert(t.reg == fake::Baud && t.raw == code + 1 && t.baud == 9600);
    assert(device.pollSensors());
  }
  std::cout << "PASS migration: one FC06, normal/lost/new-address ACK errors verified independently, all six documented baud codes\n";

  for (const auto& tuple : {std::make_pair(uint8_t(9), uint32_t(9600)),
                           std::make_pair(uint8_t(5), uint32_t(19200))}) {
    reset();
    TestHsmWt10 device;
    fake::drive.occupied = {tuple};
    esp32m::Request request;
    request.payload["address"] = tuple.first;
    request.payload["baud"] = tuple.second;
    esp32m::RequestContext context(request);
    assert(!device.setDeviceConfig(context) && !context.errors.empty());
    assertTuple(device, 5, 9600);
    assert(fake::drive.writes() == 0);
    assert(esp32m::modbus::Master::instance().config().ser_opts.baudrate == 9600);
  }
  reset();
  {
    TestHsmWt10 device;
    fake::drive.slaveErrors[9] = ESP_ERR_INVALID_RESPONSE;
    esp32m::Request request;
    request.payload["address"] = 9;
    esp32m::RequestContext context(request);
    assert(!device.setDeviceConfig(context) && !context.errors.empty());
    assert(fake::drive.writes() == 0);
  }
  std::cout << "PASS migration preflight: occupied destinations and ambiguous responses prevent writes and restore bus baud\n";

  for (const bool startupFailure : {false, true}) {
    for (const bool verificationFailure : {false, true}) {
      reset();
      TestHsmWt10 device;
      auto& master = esp32m::modbus::Master::instance();
      if (startupFailure) {
        master.failStartBaud = 19200;
        master.failStartAt = verificationFailure ? 2 : 1;
        master.startError = ESP_ERR_TIMEOUT;
      } else {
        master.failBaud = 19200;
        master.failBaudAt = verificationFailure ? 2 : 1;
        master.baudError = ESP_ERR_TIMEOUT;
      }
      esp32m::Request request;
      request.payload["baud"] = 19200;
      esp32m::RequestContext context(request);
      assert(device.setDeviceConfig(context) == verificationFailure);
      assert(!context.errors.empty());
      assert(fake::drive.writes() == (verificationFailure ? 1u : 0u));
      assertTuple(device, 5, 9600);
      assert(master.config().ser_opts.baudrate == 9600);
      assert(state(device)["communicationPending"].as<bool>() == verificationFailure);
      // After a write, a failed local bus restart is uncertain. Recovery probes
      // the actual new baud and must not transmit the change a second time.
      if (verificationFailure) {
        assert(device.pollSensors());
        assertTuple(device, 5, 19200);
        assert(fake::drive.writes() == 1);
      }
    }
  }
  reset();
  {
    TestHsmWt10 device;
    auto& master = esp32m::modbus::Master::instance();
    master.running = false;
    master.startError = ESP_FAIL;
    assert(!device.initSensors() && fake::drive.transactions.empty());
    master.startError = ESP_OK;
    assert(device.initSensors());
    master.settings.ser_opts.parity = 1;
    assert(!device.pollSensors() && fake::drive.transactions.empty());
  }
  std::cout << "PASS bus failures: startup/baud-switch timeout cannot authorize a write, failed verification retains recoverable pending state\n";

  for (const bool baudChange : {false, true}) {
    reset();
    TestHsmWt10 device;
    fake::drive.offlineAfterWrite = baudChange ? fake::Baud : fake::Address;
    esp32m::Request request;
    request.payload[baudChange ? "baud" : "address"] = baudChange ? 19200 : 9;
    esp32m::RequestContext context(request);
    assert(device.setDeviceConfig(context) && !context.errors.empty());
    assert(fake::drive.writes() == 1);
    assert(state(device)["communicationPending"].as<bool>());
    const auto pending = config(device);
    assert(pending["pendingAddress"].as<unsigned>() == (baudChange ? 5u : 9u));
    assert(pending["pendingBaud"].as<unsigned>() == (baudChange ? 19200u : 9600u));
    esp32m::RequestContext repeated(request);
    assert(!device.setDeviceConfig(repeated) && repeated.errors.error == ESP_ERR_INVALID_STATE);
    assert(fake::drive.writes() == 1);
    assert(!device.pollSensors() && fake::drive.writes() == 1);
    TestHsmWt10 rebooted;
    esp32m::Request saved(true);
    saved.payload.set(pending);
    esp32m::RequestContext restore(saved);
    assert(rebooted.setDeviceConfig(restore) && restore.errors.empty());
    assert(fake::drive.writes() == 1);
    fake::drive.offline = false;
    fake::drive.offlineAfterWrite = -1;
    assert(rebooted.initSensors() && rebooted.pollSensors());
    assertTuple(rebooted, baudChange ? 5 : 9, baudChange ? 19200 : 9600);
    assert(!state(rebooted)["communicationPending"].as<bool>());
    assert(fake::drive.writes() == 1);
  }
  std::cout << "PASS uncertainty: pending tuple persists, repeated command cannot rewrite, restart/poll resolves read-only\n";

  for (const bool pending : {false, true}) {
    for (const bool beforeApply : {false, true}) {
      reset();
      TestHsmWt10 device;
      fake::drive.address = 9;
      fake::drive.baud = pending ? 38400 : 19200;
      fake::drive.regs[fake::Address] = 9;
      fake::drive.regs[fake::Baud] = pending ? 5 : 4;
      auto& master = esp32m::modbus::Master::instance();
      master.failBaud = 19200;
      master.failBeforeBaudApply = beforeApply;
      esp32m::Request saved(true);
      saved.payload["address"] = 9;
      saved.payload["baud"] = 19200;
      if (pending) {
        saved.payload["pendingAddress"] = 9;
        saved.payload["pendingBaud"] = 38400;
      }
      esp32m::RequestContext restore(saved);
      assert(device.setDeviceConfig(restore) && !restore.errors.empty());
      assertTuple(device, 9, 19200);
      assert(state(device)["communicationPending"].as<bool>() == pending);
      assert(fake::drive.transactions.empty());
      assert(device.initSensors() && device.pollSensors());
      assertTuple(device, 9, pending ? 38400 : 19200);
      assert(fake::drive.writes() == 0);
    }
  }
  std::cout << "PASS restore failures: nondefault and pending tuples survive local UART errors and recover with reads only\n";

  reset();
  {
    TestHsmWt10 device;
    fake::drive.onRequest = [&] {
      // The configuration request holds the real production mutex here.
      // A scheduler poll on another thread must return without waiting for
      // a slow configuration transaction, and must not touch the RTU bus.
      auto scheduler = std::async(std::launch::async, [&] {
        return std::make_pair(device.pollSensors(), device.initSensors());
      });
      assert(scheduler.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready);
      const auto result = scheduler.get();
      assert(result.first && !result.second);
    };
    esp32m::Request request;
    request.payload["sensitivity"] = 400;
    esp32m::RequestContext context(request);
    assert(device.setDeviceConfig(context) && context.errors.empty());
    assert(fake::drive.transactions.size() == 3 && fake::drive.writes() == 1);
  }
  std::cout << "PASS scheduler: polling on another thread skips a configuration lock without blocking or RTU access\n";

  reset();
  {
    TestHsmWt10 device;
    fake::drive.ignoreWrite = fake::Baud;
    esp32m::Request request;
    request.payload["address"] = 9;
    request.payload["baud"] = 19200;
    esp32m::RequestContext context(request);
    assert(device.setDeviceConfig(context) && !context.errors.empty());
    assertTuple(device, 9, 9600);
    assert(fake::drive.writes() == 2);
    assert(!state(device)["communicationPending"].as<bool>());
    assert(device.pollSensors());
  }
  reset();
  {
    TestHsmWt10 device;
    auto cfg = config(device, false);
    assert(cfg["sensitivity"].as<unsigned>() == 300 && cfg["alarmDelay"].as<unsigned>() == 2);
    esp32m::Request request;
    request.payload["sensitivity"] = 500;
    request.payload["alarmDelay"] = 65535;
    fake::drive.ackError = ESP_ERR_TIMEOUT;
    esp32m::RequestContext context(request);
    assert(device.setDeviceConfig(context) && context.errors.empty());
    assert(fake::drive.regs[fake::Sensitivity] == 500 && fake::drive.regs[fake::AlarmDelay] == 65535);
    assert(fake::drive.writes() == 2);
    esp32m::RequestContext noChange(request);
    assert(!device.setDeviceConfig(noChange) && noChange.errors.empty());
    assert(fake::drive.writes() == 2);
    fake::drive.readErrors[fake::Sensitivity] = ESP_ERR_TIMEOUT;
    cfg = config(device, false);
    assert(cfg["configReadError"].as<int>() != ESP_OK);
  }
  std::cout << "PASS partial/config: verified address retained after baud failure, sensor settings read-back and no-op, config read error surfaced\n";
  reset();
  {
    TestHsmWt10 device;
    device.disabledForTest = true;
    auto cached = config(device, false);
    assert(fake::drive.transactions.empty());
    assert(cached["address"] == 5 && cached["sensitivity"].isUnbound());
  }
  std::cout << "PASS disabled settings: local config available with zero bus attempts\n";

}
