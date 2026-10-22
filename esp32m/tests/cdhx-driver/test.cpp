#include <ArduinoJson.h>
#include "esp32m/dev/cdhx/protocol.hpp"
#include "esp32m/dev/thermostat/types.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
#define logE(...) fakeLog(__VA_ARGS__)
using TaskHandle_t = void*;
using QueueHandle_t = void*;
using TickType_t = uint32_t;
using BaseType_t = int;
constexpr int pdPASS = 1, pdTRUE = 1, pdFALSE = 0;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
#define CONFIG_FREERTOS_NUMBER_OF_CORES 2
#define CONFIG_FREERTOS_HZ 100
using esp_timer_handle_t = void*;
constexpr int ESP_TIMER_TASK = 0;
struct esp_timer_create_args_t {
  void (*callback)(void*) = nullptr;
  void* arg = nullptr;
  int dispatch_method = ESP_TIMER_TASK;
  const char* name = nullptr;
  bool skip_unhandled_events = false;
};
using gpio_num_t = int;
constexpr gpio_num_t GPIO_NUM_NC = -1, GPIO_NUM_11 = 11,
    GPIO_NUM_12 = 12, GPIO_NUM_13 = 13, GPIO_NUM_14 = 14, GPIO_NUM_15 = 15;
constexpr int GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2,
    GPIO_MODE_OUTPUT_OD = 3, GPIO_PULLUP_ONLY = 1, GPIO_FLOATING = 0;
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0 && (pin) < 49)
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) GPIO_IS_VALID_GPIO(pin)
using uart_port_t = int;
constexpr uart_port_t UART_NUM_0 = 0, UART_NUM_1 = 1, UART_NUM_2 = 2;
constexpr int UART_NUM_MAX = 3, UART_DATA_8_BITS = 8,
    UART_PARITY_DISABLE = 0, UART_STOP_BITS_1 = 1,
    UART_HW_FLOWCTRL_DISABLE = 0, UART_SCLK_DEFAULT = 0,
    UART_PIN_NO_CHANGE = -1;
enum { UART_DATA, UART_FIFO_OVF, UART_BUFFER_FULL, UART_BREAK,
    UART_PARITY_ERR, UART_FRAME_ERR };
struct uart_event_t { int type = UART_DATA; size_t size = 0; bool timeout_flag = false; };
struct uart_config_t {
  int baud_rate = 0, data_bits = 0, parity = 0, stop_bits = 0,
      flow_ctrl = 0, rx_flow_ctrl_thresh = 0, source_clk = 0;
};

namespace fake {
uint64_t nowUs = 1'000'000;
struct Uart {
  std::deque<uint8_t> rx;
  std::deque<uart_event_t> events;
  std::vector<std::vector<uint8_t>> submitted;
  int txPin = -1, rxPin = -1, accept = -1;
  bool installed = false, echo = true, busy = false;
  unsigned flushes = 0;
  int installCore = -1;
};
std::array<Uart, UART_NUM_MAX> uarts;
std::array<int, 49> pinModes{}, pinLevels{};
std::vector<std::string> actions;
bool initialized = true;
unsigned stateEvents = 0;
unsigned sensorWrites = 0;
unsigned configEvents = 0;
bool inOwnerStep = false, configPublishedInOwner = false;
unsigned broadcastEvents = 0;
struct DeferredResponse {
  unsigned published = 0;
  bool error = false;
  JsonDocument data;
};
void (*pendingWorker)(void*) = nullptr;
void* pendingWorkerArgument = nullptr;
bool failCreateWorker = false;
unsigned createdWorkers = 0;
std::function<void()> afterTaskDelay;
void runPendingWorker() {
  const auto entry = pendingWorker;
  const auto argument = pendingWorkerArgument;
  pendingWorker = nullptr;
  pendingWorkerArgument = nullptr;
  if (entry) entry(argument);
}
void* pendingTask = nullptr;
void* taskOwner = nullptr;
int currentCore = 0, requestedCore = -1, requestedPriority = -1;
int failInstallUart = -1, failParamUart = -1;
bool failCreateTask = false, failCreateTimer = false;
unsigned timerStarts = 0, timerStops = 0, timerDeletes = 0;
uint64_t timerIntervalUs = 0;
bool timerActive = false, ownerExitedWithActiveTimer = false;
std::function<void(uart_port_t)> afterUartRead;
std::function<void(uart_port_t)> afterBufferedDataQuery;
void bootstrapPendingTask();
void completeStoppedTask();
void reset() {
  nowUs = 1'000'000;
  uarts = {};
  pinModes.fill(GPIO_MODE_INPUT);
  pinLevels.fill(1);
  actions.clear();
  initialized = true;
  stateEvents = 0;
  sensorWrites = 0;
  configEvents = 0;
  inOwnerStep = configPublishedInOwner = false;
  broadcastEvents = 0;
  pendingWorker = nullptr;
  pendingWorkerArgument = nullptr;
  failCreateWorker = false;
  createdWorkers = 0;
  afterTaskDelay = {};
  pendingTask = nullptr;
  taskOwner = nullptr;
  currentCore = 0;
  requestedCore = requestedPriority = -1;
  failInstallUart = failParamUart = -1;
  failCreateTask = failCreateTimer = false;
  timerStarts = timerStops = timerDeletes = 0;
  timerIntervalUs = 0;
  timerActive = ownerExitedWithActiveTimer = false;
  afterUartRead = {};
  afterBufferedDataQuery = {};
}
void inject(int uart, const esp32m::dev::cdhx::protocol::Frame& frame) {
  uarts[uart].rx.insert(uarts[uart].rx.end(), frame.begin(), frame.end());
}
}  // namespace fake

int64_t esp_timer_get_time() { return static_cast<int64_t>(fake::nowUs); }
#if !defined(ESP32M_CDHX_RESEARCH) || !ESP32M_CDHX_RESEARCH
void esp_fill_random(void* target, size_t size) { std::memset(target, 0x5a, size); }
#endif
const char* esp_err_to_name(esp_err_t) { return "fake error"; }
int xTaskCreate(void (*entry)(void*), const char*, unsigned, void* argument, unsigned,
                TaskHandle_t* task) {
  if (fake::failCreateWorker) return pdFALSE;
  fake::pendingWorker = entry;
  fake::pendingWorkerArgument = argument;
  ++fake::createdWorkers;
  if (task) *task = reinterpret_cast<void*>(1);
  return pdPASS;
}
int xTaskCreatePinnedToCore(void (*)(void*), const char*, unsigned, void* context,
                          unsigned priority, TaskHandle_t* task, int core) {
  if (fake::failCreateTask) return pdFALSE;
  if (task) *task = reinterpret_cast<void*>(1);
  fake::pendingTask = context;
  fake::taskOwner = context;
  fake::requestedCore = core;
  fake::requestedPriority = static_cast<int>(priority);
  return pdPASS;
}
void vTaskDelete(TaskHandle_t) {}
void vTaskDelay(TickType_t ticks) {
  fake::nowUs += uint64_t(ticks) * (1'000'000 / CONFIG_FREERTOS_HZ);
  if (fake::pendingTask) fake::bootstrapPendingTask();
  if (fake::taskOwner) fake::completeStoppedTask();
  if (fake::pendingWorker) fake::runPendingWorker();
  if (fake::afterTaskDelay) fake::afterTaskDelay();
}
void xTaskNotifyGive(TaskHandle_t) {}
uint32_t ulTaskNotifyTake(int, TickType_t) { fake::nowUs += 1000; return 1; }
esp_err_t esp_timer_create(const esp_timer_create_args_t*, esp_timer_handle_t* timer) {
  if (fake::failCreateTimer) return ESP_FAIL;
  *timer = reinterpret_cast<void*>(1);
  return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t, uint64_t interval) {
  ++fake::timerStarts;
  fake::timerIntervalUs = interval;
  fake::timerActive = true;
  return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t) {
  ++fake::timerStops;
  fake::timerActive = false;
  fake::actions.push_back("timer-stop");
  return ESP_OK;
}
esp_err_t esp_timer_stop_blocking(esp_timer_handle_t timer, uint32_t) {
  return esp_timer_stop(timer);
}
esp_err_t esp_timer_delete(esp_timer_handle_t) { ++fake::timerDeletes; return ESP_OK; }
esp_err_t esp_task_wdt_add(void*) { return ESP_OK; }
esp_err_t esp_task_wdt_status(void*) { return ESP_OK; }
esp_err_t esp_task_wdt_reset() { return ESP_OK; }
esp_err_t esp_task_wdt_delete(void*) { return ESP_OK; }
esp_err_t gpio_reset_pin(gpio_num_t pin) {
  fake::pinModes[pin] = GPIO_MODE_INPUT;
  fake::actions.push_back("release:" + std::to_string(pin));
  return ESP_OK;
}
esp_err_t gpio_set_direction(gpio_num_t pin, int mode) {
  fake::pinModes[pin] = mode;
  return ESP_OK;
}
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level) {
  fake::pinLevels[pin] = static_cast<int>(level);
  fake::actions.push_back("level:" + std::to_string(pin) + ":" + std::to_string(level));
  return ESP_OK;
}
int gpio_get_level(gpio_num_t pin) { return fake::pinLevels[pin]; }
esp_err_t gpio_pullup_en(gpio_num_t) { return ESP_OK; }
esp_err_t gpio_pulldown_dis(gpio_num_t) { return ESP_OK; }
esp_err_t gpio_set_pull_mode(gpio_num_t, int) { return ESP_OK; }
esp_err_t uart_param_config(uart_port_t uart, const uart_config_t*) {
  return uart == fake::failParamUart ? ESP_FAIL : ESP_OK;
}
esp_err_t uart_driver_install(uart_port_t uart, int, int, int, QueueHandle_t* queue, int) {
  if (uart == fake::failInstallUart) return ESP_FAIL;
  fake::uarts[uart].installed = true;
  fake::uarts[uart].installCore = fake::currentCore;
  if (queue) *queue = &fake::uarts[uart];
  return ESP_OK;
}
esp_err_t uart_driver_delete(uart_port_t uart) {
  fake::uarts[uart].installed = false;
  return ESP_OK;
}
bool uart_is_driver_installed(uart_port_t uart) { return fake::uarts[uart].installed; }
esp_err_t uart_set_pin(uart_port_t uart, int tx, int rx, int, int) {
  if (tx != UART_PIN_NO_CHANGE) {
    fake::uarts[uart].txPin = tx;
    fake::pinModes[tx] = GPIO_MODE_OUTPUT;
  }
  if (rx != UART_PIN_NO_CHANGE) fake::uarts[uart].rxPin = rx;
  return ESP_OK;
}
esp_err_t uart_set_rx_full_threshold(uart_port_t, int) { return ESP_OK; }
esp_err_t uart_set_rx_timeout(uart_port_t, int) { return ESP_OK; }
esp_err_t uart_get_buffered_data_len(uart_port_t uart, size_t* size) {
  fake::actions.push_back("query:" + std::to_string(uart));
  *size = fake::uarts[uart].rx.size();
  if (fake::afterBufferedDataQuery) fake::afterBufferedDataQuery(uart);
  return ESP_OK;
}
int uart_read_bytes(uart_port_t uart, void* target, uint32_t capacity, TickType_t) {
  auto& rx = fake::uarts[uart].rx;
  auto* bytes = static_cast<uint8_t*>(target);
  const auto size = std::min<size_t>(capacity, rx.size());
  for (size_t i = 0; i < size; ++i) { bytes[i] = rx.front(); rx.pop_front(); }
  if (fake::afterUartRead) fake::afterUartRead(uart);
  return static_cast<int>(size);
}
esp_err_t uart_wait_tx_done(uart_port_t uart, TickType_t) {
  return fake::uarts[uart].busy ? ESP_ERR_TIMEOUT : ESP_OK;
}
int uart_tx_chars(uart_port_t uart, const char* bytes, uint32_t size) {
  auto& endpoint = fake::uarts[uart];
  const int accepted = endpoint.accept < 0 ? static_cast<int>(size)
      : std::min(endpoint.accept, static_cast<int>(size));
  endpoint.submitted.emplace_back(bytes, bytes + accepted);
  fake::actions.push_back("tx:" + std::to_string(uart));
  if (endpoint.echo)
    endpoint.rx.insert(endpoint.rx.end(), bytes, bytes + accepted);
  return accepted;
}
int uart_write_bytes(uart_port_t uart, const void* bytes, size_t size) {
  return uart_tx_chars(uart, static_cast<const char*>(bytes), static_cast<uint32_t>(size));
}
esp_err_t uart_flush_input(uart_port_t uart) {
  fake::uarts[uart].rx.clear();
  ++fake::uarts[uart].flushes;
  fake::actions.push_back("flush:" + std::to_string(uart));
  return ESP_OK;
}
int xQueueReceive(QueueHandle_t queue, void* target, TickType_t) {
  if (!queue) return pdFALSE;
  auto& events = static_cast<fake::Uart*>(queue)->events;
  if (events.empty()) return pdFALSE;
  *static_cast<uart_event_t*>(target) = events.front();
  events.pop_front();
  return pdTRUE;
}
int xQueueReset(QueueHandle_t queue) {
  if (queue) static_cast<fake::Uart*>(queue)->events.clear();
  return pdTRUE;
}
void vQueueDelete(QueueHandle_t) {}
unsigned uxQueueMessagesWaiting(QueueHandle_t queue) {
  return queue ? static_cast<unsigned>(static_cast<fake::Uart*>(queue)->events.size()) : 0;
}

namespace esp32m {
struct Response;
struct Request {
  JsonDocument payload, response;
  std::string requestedName;
  bool broadcast = false, internal = false;
  bool failResponseCreation = false;
  esp_err_t responseError = ESP_OK;
  std::shared_ptr<fake::DeferredResponse> deferred = std::make_shared<fake::DeferredResponse>();
  Request() = default;
  Request(const char* name, int, const char* target, JsonVariantConst data, const char* origin)
      : requestedName(name ? name : ""), broadcast(target == nullptr), internal(origin == nullptr) {
    payload.set(data);
  }
  virtual ~Request() = default;
  JsonVariantConst data() const { return payload.as<JsonVariantConst>(); }
  bool is(const char* name) const { return requestedName == name; }
  bool isBroadcast() const { return broadcast; }
  bool isInternal() const { return internal; }
  void respondError(esp_err_t error, const char*) { responseError = error; }
  void respond(esp_err_t error) { responseError = error; }
  void respond(const char*, const JsonDocument& data, bool) { response.set(data); }
  Response* makeResponse();
 protected:
  virtual void respondImpl(const char*, JsonVariantConst, bool) {}
};
struct Response {
  std::shared_ptr<fake::DeferredResponse> state;
  JsonDocument ownedRequest;
  std::unique_ptr<JsonDocument> ownedData;
  bool error = false;
  explicit Response(Request& request) : state(request.deferred) { ownedRequest.set(request.data()); }
  JsonDocument* requestData() { return &ownedRequest; }
  void setError(esp_err_t code) {
    ownedData = std::make_unique<JsonDocument>();
    ownedData->set(code);
    error = code != ESP_OK;
  }
  void setError(JsonDocument* data) { ownedData.reset(data); error = true; }
  void setData(JsonDocument* data) { ownedData.reset(data); error = false; }
  void publish() {
    ++state->published;
    state->error = error;
    if (ownedData) state->data.set(*ownedData);
    fake::configPublishedInOwner |= fake::inOwnerStep;
  }
};
Response* Request::makeResponse() { return failResponseCreation ? nullptr : new Response(*this); }
struct ErrorList {
  esp_err_t error = ESP_OK;
  std::string message;
  void check(esp_err_t value, const char* = nullptr) { if (value != ESP_OK) error = value; }
  template<class... T> void add(const char* value, T...) { message = value; error = ESP_ERR_INVALID_ARG; }
  bool empty() const { return error == ESP_OK; }
  void toJson(JsonVariant target) const { if (error != ESP_OK) target.to<JsonArray>().add(error); }
};
struct RequestContext {
  Request& request;
  JsonVariantConst data;
  ErrorList errors;
  std::unique_ptr<JsonDocument> result;
  explicit RequestContext(Request& value) : request(value), data(value.data()) {}
  RequestContext(Request& value, JsonVariantConst input) : request(value), data(input) {}
};
struct Device {
  enum Flags { None = 0, HasSensors = 1 };
  virtual ~Device() = default;
  virtual const char* name() const { return "fake"; }
  void init(int) {}
  void setSensorsPollInterval(unsigned) {}
  virtual const JsonVariantConst descriptor() const { return {}; }
  virtual JsonDocument* getState(RequestContext&) { return nullptr; }
  virtual void setState(RequestContext&) {}
  virtual JsonDocument* getConfig(RequestContext&) { return nullptr; }
  virtual bool setConfig(RequestContext&) { return false; }
  virtual bool pollSensors() { return false; }
  const char* interactiveName() const { return name(); }
  virtual bool handleRequest(Request&) { return false; }
};
struct Broadcast { static void publish(const char*, const char*) {
  ++fake::broadcastEvents;
  fake::configPublishedInOwner |= fake::inOwnerStep;
} };
namespace config {
struct Changed {
  static void publish(Device*) {
    ++fake::configEvents;
    fake::configPublishedInOwner |= fake::inOwnerStep;
  }
};
}  // namespace config
struct App { static bool initialized() { return fake::initialized; } };
struct EventStateChanged { static void publish(Device*) { ++fake::stateEvents; } };
enum class StateClass { Measurement };
struct Sensor {
  const char* unit = nullptr;
  int precision = 0;
  StateClass stateClass = StateClass::Measurement;
  JsonDocument state;
  bool valid = false;
  uint64_t sampledAtMs = 0;
  Sensor(Device*, const char*, const char*) {}
  void setTitle(const char*) {}
  template<class T> void set(T value) { state.set(value); valid = true; ++fake::sensorWrites; }
  template<class T> void setSample(T value, uint64_t stamp) { set(value); sampledAtMs = stamp; }
  void resetState() { state.clear(); valid = false; ++fake::sensorWrites; }
};
}  // namespace esp32m

// Access transport state and a single deterministic owner iteration without
// introducing a second implementation of the production state machine.
#define private public
#define protected public
#include "production.inc"
#undef protected
#undef private

namespace p = esp32m::dev::cdhx::protocol;

void fake::bootstrapPendingTask() {
  auto* owner = static_cast<esp32m::dev::Cdhx::Impl*>(pendingTask);
  pendingTask = nullptr;
  const auto priorCore = currentCore;
  currentCore = requestedCore;
  owner->_startupResult = owner->initializeHardware();
  owner->_startupDone.store(true);
  if (owner->_startupResult != ESP_OK) {
    owner->_task.store(nullptr);
    taskOwner = nullptr;
  }
  else owner->_armAfter = fake::nowUs + uint64_t(owner->_options.quietBeforeArmMs) * 1000;
  currentCore = priorCore;
}

void fake::completeStoppedTask() {
  auto* owner = static_cast<esp32m::dev::Cdhx::Impl*>(taskOwner);
  if (!owner->_stop.load()) return;
  ownerExitedWithActiveTimer = timerActive;
  owner->_task.store(nullptr);
  taskOwner = nullptr;
}

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

p::Frame stockRequest() {
  // Published protocol example has independently known high-first CRC DC ED.
  return {0x78,0x16,0x00,0x16,0x23,0x10,0x37,0x06,0x90,0x11,0x94,0xf0,
          0x01,0x32,0x08,0x23,0x05,0x00,0x01,0x2c,0x0d,0xac,0xdc,0xed};
}

p::Frame heaterResponse(uint8_t runState = 0, uint8_t status = 0) {
  p::Frame frame{0x76,0x16,runState,status,0x00,0x81,0x01,0xf4,0x00,0x40,
                0xff,0xfb,0x00,0x7c,0x00,0x64,0x17,0x55,0x02,0x23,0x64,0x00};
  p::updateCrc(frame);
  return frame;
}

p::Frame fullyStoppedResponse();

void testProtocol() {
  constexpr uint8_t crcReference[] = {'1','2','3','4','5','6','7','8','9'};
  check(p::crc16(crcReference, sizeof(crcReference)) == 0x4b37, "CRC independent reference");
  const auto stock = stockRequest();
  check(p::validRequest(stock), "published stock request CRC");
  check(!p::validResponse(stock), "direction must not decode rotary request as response");
  auto reversed = stock;
  std::swap(reversed[22], reversed[23]);
  check(!p::validRequest(reversed), "little-first wire CRC must be rejected");
  auto patched = stock;
  check(p::patchCommand(patched, p::StartCommand), "patch valid start");
  check(p::validRequest(patched), "patched request CRC");
  for (size_t i = 0; i < 22; ++i)
    check(patched[i] == (i == 2 ? p::StartCommand : stock[i]), "start altered tuning bytes");
  patched = stock;
  check(!p::patchCommand(patched, 0x5a), "unknown command rejected");
  check(patched == stock, "rejected command mutation");
  check(p::patchTemperature(patched, 18), "in-bounds thermostat override");
  for (size_t i = 0; i < 22; ++i)
    check(patched[i] == (i == 4 ? 18 : stock[i]), "temperature altered tuning bytes");
  check(!p::patchTemperature(patched, 7), "below captured minimum rejected");
  check(!p::patchTemperature(patched, 36), "above captured maximum rejected");
  auto fixed = stock;
  fixed[13] = p::FixedPowerMode;
  p::updateCrc(fixed);
  check(!p::patchTemperature(fixed, 18), "fixed-power raw must not accept Celsius");
  auto signedBounds = stock;
  signedBounds[14] = 0xf8;
  p::updateCrc(signedBounds);
  check(!p::patchTemperature(signedBounds, 18), "unsupported negative bounds rejected");
  auto negative = stock;
  negative[3] = 0xfb;
  p::updateCrc(negative);
  p::Request request;
  check(p::decodeRequest(negative, request) && request.ambientTemperatureC == -5,
        "ambient signed decode");
  p::Response response;
  check(p::decodeResponse(heaterResponse(), response) && response.bodyTemperatureC == -5,
        "body signed decode");
  check(response.retained17 == 0x55 && response.auxiliary18 == 2 &&
        response.constant20 == 100, "opaque response fields retained");

  p::Parser parser;
  p::Frame completed;
  uint64_t now = 0;
  check(!parser.feed(0x99, now++, completed), "noise must not complete frame");
  check(!parser.feed(0x76, now++, completed), "false header partial");
  check(!parser.feed(0x15, now++, completed), "invalid length rejected");
  for (size_t i = 0; i < stock.size(); ++i)
    check(parser.feed(stock[i], now += 400, completed) == (i + 1 == stock.size()),
          "stream parser frame boundary");
  check(completed == stock && parser.stats().frames == 1 &&
        parser.stats().lengthErrors == 1, "parser recovery and counters");
  check(!parser.feed(0x76, now += 400, completed), "partial header");
  check(parser.expire(now + p::FrameGapUs), "truncation expires at boundary");
  check(parser.stats().truncated == 1 && parser.buffered() == 0, "truncation state");
  std::cout << "protocol: CRC, preservation, signed decode, parser recovery passed\n";
}

using Driver = esp32m::dev::Cdhx;

struct Fixture {
  std::unique_ptr<Driver> driver;
  explicit Fixture(bool powerEnabled = false, bool bypassEnabled = false,
                   int interceptLevel = 1, uint32_t responseTimeoutMs = 200) {
    fake::reset();
    Driver::Options options({GPIO_NUM_13, GPIO_NUM_14, UART_NUM_2},
                            {GPIO_NUM_11, GPIO_NUM_12, UART_NUM_1});
    if (powerEnabled) {
      options.fixedPowerMinimumRaw = 13;
      options.fixedPowerMaximumRaw = 14;
    }
    if (bypassEnabled) options.bypassPin = GPIO_NUM_15;
    options.interceptLevel = interceptLevel;
    options.responseTimeoutMs = responseTimeoutMs;
    driver = std::make_unique<Driver>("test-heater", options);
    tick(21'000);
    check(driver->_impl->_active == !bypassEnabled,
          "bridge boot activation does not match bypass qualification policy");
  }
  ~Fixture() {
    // The fake owner observes _stop cooperatively from vTaskDelay during the
    // actual production destructor's wait; do not pre-clear its task handle.
  }
  void tick(uint64_t advanceUs = 1000) {
    fake::nowUs += advanceUs;
    fake::inOwnerStep = true;
    driver->_impl->step();
    fake::inOwnerStep = false;
    driver->pollSensors();
  }
  void request(const p::Frame& frame = stockRequest()) {
    fake::inject(UART_NUM_1, frame);
    tick();
  }
  void reply(const p::Frame& frame = heaterResponse()) {
    fake::inject(UART_NUM_2, frame);
    tick();
    tick(); // Consume own echo of the response forwarded to the display.
  }
  void exchange(const p::Frame& requestFrame = stockRequest(),
                const p::Frame& replyFrame = heaterResponse()) {
    request(requestFrame);
    reply(replyFrame);
  }
  void mirror(const p::Frame& frame) {
    fake::inject(UART_NUM_1, frame);
    fake::inject(UART_NUM_2, frame);
    tick();
  }
  void qualify(unsigned pairs = 3) {
    for (unsigned i = 0; i < pairs; ++i) {
      mirror(stockRequest());
      tick(20'000);
      mirror(heaterResponse());
      if (i + 1 < pairs) tick(250'000);
    }
  }
  void enterIntercept() {
    qualify();
    tick(20'000);
    check(!driver->_impl->_bypassActive && !driver->_impl->_active,
          "bypass was not opened before settle interval");
    assertReleased();
    tick(9'000);
    check(!driver->_impl->_active, "TX attached before switch settle time");
    tick(1'000);
    check(driver->_impl->_active, "qualified switch did not enable bridge after settling");
  }
  p::Frame outgoing() const {
    const auto& sent = fake::uarts[UART_NUM_2].submitted;
    check(!sent.empty() && sent.back().size() == p::FrameSize, "no complete outgoing request");
    p::Frame result;
    std::copy(sent.back().begin(), sent.back().end(), result.begin());
    return result;
  }
  JsonDocument state() const {
    JsonDocument result;
    driver->_impl->toState(result.to<JsonObject>());
    return result;
  }
  bool faulted() const { return !driver->_impl->_fault.empty(); }
  void assertReleased() const {
    check(fake::pinModes[GPIO_NUM_12] == GPIO_MODE_INPUT &&
          fake::pinModes[GPIO_NUM_14] == GPIO_MODE_INPUT, "fault did not detach both outputs");
  }
  void rearm() {
    check(driver->resetBridge() == ESP_OK, "explicit fault rearm rejected");
    tick();
    tick(21'000);
    check(!faulted() && driver->_impl->_active, "explicit fault rearm did not arm");
  }
};

void testAcquisitionTimes() {
  Fixture fixture;
  const auto initial = fixture.state();
  check(initial["display"]["sampledAtMs"] == 0 &&
        initial["heater"]["sampledAtMs"] == 0,
        "never-read serial frame timestamps must preserve zero sentinel");
  fixture.exchange();
  const auto observed = fixture.state();
  const auto displayAt = fixture.driver->_impl->_displayStamp;
  const auto heaterAt = fixture.driver->_impl->_heaterStamp;
  check(observed["display"]["sampledAtMs"].as<uint64_t>() == displayAt / 1000 &&
        observed["heater"]["sampledAtMs"].as<uint64_t>() == heaterAt / 1000 &&
        heaterAt > displayAt,
        "independently acquired request/response must retain actual timestamps");
  fake::nowUs += 123;
  const auto repeated = fixture.state();
  check(repeated["display"]["sampledAtMs"] == observed["display"]["sampledAtMs"] &&
        repeated["heater"]["sampledAtMs"] == observed["heater"]["sampledAtMs"] &&
        repeated["display"]["ageMs"].isUnbound() &&
        repeated["heater"]["ageMs"].isUnbound() &&
        repeated["configuration"]["uptimeMs"].isUnbound(),
        "serialization must not emit advancing ages or uptime");
  const auto late = (uint64_t(1) << 33) * 1000 + 123;
  fixture.driver->_impl->_displayStamp = late;
  fixture.driver->_impl->_heaterStamp = late + 2345;
  fake::nowUs = late + 3000;
  const auto lateState = fixture.state();
  check(lateState["display"]["sampledAtMs"].as<uint64_t>() == late / 1000 &&
        lateState["heater"]["sampledAtMs"].as<uint64_t>() == (late + 2345) / 1000,
        "frame acquisition timestamps must not narrow at 32-bit millis rollover");
  std::cout << "timestamps: exact independent frame acquisition, stable serialization and 64-bit monotonic clock passed\n";
}

void testForwardingAndOneShot() {
  Fixture fixture;
  check(fixture.driver->turn(true) == ESP_ERR_INVALID_STATE, "control before fresh telemetry");
  fixture.exchange();
  check(fixture.outgoing() == stockRequest(), "stock request changed by transparent bridge");
  check(fake::uarts[UART_NUM_1].submitted.size() == 1 &&
        fake::uarts[UART_NUM_2].submitted.size() == 1,
        "own echo produced forwarding loop");
  check(fixture.driver->turn(true) == ESP_OK, "fresh standby start rejected");
  check(fixture.driver->turn(true) == ESP_ERR_INVALID_STATE, "duplicate queued start allowed");
  fixture.request();
  check(fixture.outgoing()[2] == p::StartCommand, "start pulse missing");
  check(fixture.state()["control"]["lastCommandResult"] == "sent-awaiting-observation",
        "transmission mislabeled as acknowledged outcome");
  fixture.reply(); // Valid reply, but state remains standby: no command ACK.
  check(fixture.driver->turn(true) == ESP_ERR_INVALID_STATE, "unconfirmed start can be queued twice");
  fixture.request();
  check(fixture.outgoing()[2] == 0, "start pulse repeated in next transaction");
  fixture.reply(heaterResponse(1, 1));
  check(fixture.state()["control"]["lastCommandResult"] == "observed-start-sequence",
        "later state feedback did not resolve observation");
  check(fixture.driver->turn(false) == ESP_OK, "fresh starting-state stop rejected");
  fixture.request();
  check(fixture.outgoing()[2] == p::StopCommand, "stop pulse missing");
  fixture.reply(heaterResponse(6, 1));
  check(fixture.state()["control"]["lastCommandResult"] == "observed-shutdown-sequence",
        "shutdown feedback missing");
  fixture.exchange(stockRequest(), heaterResponse(8, 0));
  check(fixture.outgoing()[2] == 0, "stop pulse repeated");
  check(fixture.driver->turn(true) == ESP_ERR_INVALID_STATE, "start during cooldown accepted");
  std::cout << "runtime: transparent forwarding, exact echo, once-only command observation passed\n";
}

void testLocalArbitration() {
  Fixture fixture;
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  check(fixture.driver->setTemperature(18) == ESP_OK, "persistent thermostat setpoint rejected");
  auto ambientChanged = stockRequest();
  ambientChanged[3] = 20;
  p::updateCrc(ambientChanged);
  fixture.exchange(ambientChanged);
  auto expected = ambientChanged;
  expected[4] = 18;
  p::updateCrc(expected);
  check(fixture.outgoing() == expected,
        "fresh physical ambient failed to accompany persistent setpoint");
  auto localSetpoint = ambientChanged;
  localSetpoint[4] = 34;
  p::updateCrc(localSetpoint);
  fixture.exchange(localSetpoint);
  expected = localSetpoint;
  expected[4] = 18;
  p::updateCrc(expected);
  check(fixture.outgoing() == expected,
        "physical display setpoint replaced persistent driver authority");
  auto tuningChanged = localSetpoint;
  tuningChanged[5] += 1;
  p::updateCrc(tuningChanged);
  fixture.exchange(tuningChanged);
  expected = tuningChanged;
  expected[4] = 18;
  p::updateCrc(expected);
  check(fixture.outgoing() == expected,
        "physical service change cleared persistent operating authority");
  check(fixture.driver->turn(true) == ESP_OK, "pending explicit Start rejected");
  auto localStop = tuningChanged;
  localStop[2] = p::StopCommand;
  p::updateCrc(localStop);
  fixture.exchange(localStop);
  expected = localStop;
  expected[4] = 18;
  p::updateCrc(expected);
  check(fixture.outgoing() == expected &&
        fixture.state()["control"]["lastCommandResult"] == "cancelled-by-display",
        "physical Stop did not cancel pending Start or lost persistent setpoint");
  auto prime = tuningChanged;
  prime[17] = 0x5a;
  p::updateCrc(prime);
  fixture.exchange(prime);
  expected = prime;
  expected[4] = 18;
  p::updateCrc(expected);
  check(fixture.outgoing() == expected, "persistent operating patch altered physical priming");
  check(fixture.driver->turn(true) == ESP_ERR_INVALID_STATE &&
        fixture.driver->setTemperature(19) == ESP_ERR_INVALID_STATE,
        "new remote edit allowed during stock prime");
  std::cout << "runtime: persistent setpoint priority, physical command cancellation and priming preservation passed\n";
}
void testFixedPowerBounds() {
  auto fixed = stockRequest();
  fixed[13] = p::FixedPowerMode;
  fixed[3] = 0;
  fixed[4] = 13;
  p::updateCrc(fixed);
  {
    Fixture fixture;
    fixture.exchange(fixed, fullyStoppedResponse());
    check(fixture.driver->setFixedPower(14) != ESP_OK,
          "unconfigured raw power control enabled");
  }
  {
    Fixture fixture(true);
    fixture.exchange(fixed, fullyStoppedResponse());
    check(fixture.driver->setTemperature(18) == ESP_ERR_INVALID_STATE,
          "Celsius control accepted in fixed mode");
    check(fixture.driver->setFixedPower(12) == ESP_ERR_INVALID_ARG &&
          fixture.driver->setFixedPower(15) == ESP_ERR_INVALID_ARG,
          "raw power outside explicit range accepted");
    check(fixture.driver->setFixedPower(14) == ESP_OK, "explicit raw power control rejected");
    fixture.exchange(fixed);
    auto expected = fixed;
    expected[4] = 14;
    p::updateCrc(expected);
    check(fixture.outgoing() == expected, "fixed override changed other template fields");
    check(fixture.driver->releaseOverride() == ESP_OK, "release override rejected");
    fixture.exchange(fixed);
    check(fixture.outgoing() == expected, "release discarded persistent raw-power setting");
  }
  std::cout << "runtime: explicit fixed-power bounds and permanent setpoint retention passed\n";
}

void testFaultsAndNoReplay() {
  {
    Fixture fixture;
    fixture.exchange();
    check(fixture.driver->turn(true) == ESP_OK, "start before timeout test");
    fixture.request();
    check(fixture.outgoing()[2] == p::StartCommand, "timeout start not sent");
    fixture.tick(201'000);
    check(fixture.faulted(), "missing heater reply did not latch fault");
    fixture.assertReleased();
    check(fixture.state()["control"]["lastCommandResult"] == "uncertain-no-retry",
          "missing reply did not preserve uncertain command result");
    const auto submissions = fake::uarts[UART_NUM_2].submitted.size();
    fixture.request();
    check(fake::uarts[UART_NUM_2].submitted.size() == submissions, "faulted bridge transmitted");
    fixture.rearm();
    fixture.exchange();
    check(fixture.outgoing()[2] == 0 &&
          fixture.state()["control"]["temperatureOverrideC"].isNull(),
          "fault rearm replayed command or override");
  }
  {
    Fixture fixture;
    fixture.exchange();
    fake::uarts[UART_NUM_2].echo = false;
    fixture.request();
    fixture.tick(20'000);
    check(fixture.faulted(), "missing own echo did not latch fault");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    fixture.exchange();
    fake::uarts[UART_NUM_2].echo = false;
    fixture.request();
    fake::uarts[UART_NUM_2].rx.push_back(0x00);
    fixture.tick();
    check(fixture.faulted(), "mismatched own echo did not latch fault");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    fixture.exchange();
    check(fixture.driver->turn(true) == ESP_OK, "start before partial TX test");
    fake::uarts[UART_NUM_2].accept = 3;
    fixture.request();
    check(fixture.faulted() && fixture.state()["control"]["lastCommandResult"] == "uncertain-no-retry",
          "partial command TX did not latch uncertainty");
    fixture.assertReleased();
    check(fake::uarts[UART_NUM_2].submitted.back().size() == 3, "partial TX fake not exercised");
  }
  {
    Fixture fixture;
    fixture.exchange();
    auto corrupt = stockRequest();
    corrupt[6] ^= 1;
    const auto submissions = fake::uarts[UART_NUM_2].submitted.size();
    fixture.request(corrupt);
    check(fixture.faulted() && fake::uarts[UART_NUM_2].submitted.size() == submissions,
          "CRC-invalid request forwarded");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    fixture.exchange();
    fake::uarts[UART_NUM_1].rx.push_back(0x76);
    fixture.tick();
    fixture.tick(p::FrameGapUs);
    check(fixture.faulted(), "truncated packet did not latch fault");
    fixture.assertReleased();
  }
  std::cout << "runtime: reply/echo timeout, mismatch, partial TX, CRC/truncation and no replay passed\n";
}

void testPreflightAndPublication() {
  {
    Fixture fixture;
    fixture.exchange();
    fake::initialized = false;
    fake::sensorWrites = 0;
    fixture.driver->_impl->_sensorDirty = true;
    fixture.driver->_impl->updateSensors();
    check(fake::sensorWrites == 0, "component events emitted before App::Init completes");
    fake::initialized = true;
    fixture.driver->_impl->updateSensors();
    check(fake::sensorWrites > 0, "suppressed pre-init sensor update was lost");
    fixture.exchange(stockRequest(), heaterResponse(0, 10));
    check(!fixture.driver->_impl->_body.valid, "sensor-fault body reading published as valid");
  }
  {
    Fixture fixture;
    fixture.exchange();
    fake::actions.clear();
    fake::uarts[UART_NUM_2].events.push_back({UART_FRAME_ERR, 0, false});
    const auto submissions = fake::uarts[UART_NUM_2].submitted.size();
    fixture.request();
    check(fixture.faulted(), "queued destination UART fault not latched");
    check(fake::uarts[UART_NUM_2].submitted.size() == submissions,
          "forwarded request before inspecting pending destination UART fault");
    fixture.assertReleased();
    check(!fake::actions.empty() && fake::actions.front().find("release:") == 0,
          "UART error flushed/formatted before output release");
  }
  {
    Fixture fixture;
    fixture.exchange();
    fake::pinLevels[GPIO_NUM_13] = 0;
    const auto submissions = fake::uarts[UART_NUM_2].submitted.size();
    fixture.request();
    check(fixture.faulted() && fake::uarts[UART_NUM_2].submitted.size() == submissions,
          "request sent while destination DATA was low");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    fixture.exchange();
    fake::uarts[UART_NUM_2].busy = true;
    const auto submissions = fake::uarts[UART_NUM_2].submitted.size();
    fixture.request();
    check(fixture.faulted() && fake::uarts[UART_NUM_2].submitted.size() == submissions,
          "request sent while destination UART was busy");
    fixture.assertReleased();
  }
  std::cout << "runtime: UART/DATA preflight, immediate release and initialized-only sensor publication passed\n";
}

void testOptionalBypass() {
  {
    Fixture fixture;
    check(fixture.driver->setBypass(true) == ESP_ERR_INVALID_STATE,
          "unconfigured bypass GPIO API accepted");
  }
  {
    Fixture fixture(false, true);
    check(fake::pinLevels[GPIO_NUM_15] == 0 && fixture.driver->_impl->_bypassActive,
          "configured board did not boot in stock bypass");
    fixture.assertReleased();
    check(fixture.driver->turn(true) == ESP_ERR_INVALID_STATE, "control allowed during bypass qualification");
    fixture.enterIntercept();
    check(fake::pinLevels[GPIO_NUM_15] == 1, "configured intercept polarity incorrect");
    check(fake::uarts[UART_NUM_1].submitted.empty() && fake::uarts[UART_NUM_2].submitted.empty(),
          "bypass qualification actively transmitted");
    check(fixture.state()["control"]["emittedRequestHex"].isNull() &&
          fixture.state()["control"]["emittedRequestedRaw"].isNull(),
          "passive bypass capture invented an emitted request");
    fixture.exchange();
    check(fixture.state()["control"]["emittedRequestHex"].is<const char*>() &&
          fixture.state()["control"]["emittedRequestedRaw"].as<int>() == stockRequest()[4],
          "actual transmission missing from emitted-request telemetry");
    check(fixture.driver->turn(true) == ESP_OK, "queued command before requested bypass");
    fake::actions.clear();
    check(fixture.driver->setBypass(true) == ESP_OK, "requested bypass rejected");
    fixture.assertReleased();
    check(fake::pinLevels[GPIO_NUM_15] == 0 && fixture.driver->_impl->_bypassActive &&
          fixture.driver->_impl->_bypassRequested, "requested bypass not selected immediately");
    check(fake::actions.size() >= 3 && fake::actions[0] == "release:12" &&
          fake::actions[1] == "release:14" && fake::actions[2] == "level:15:0",
          "bypass selected before both transmitters were detached");
    fixture.qualify();
    fixture.tick(100'000);
    check(!fixture.driver->_impl->_active && fixture.driver->_impl->_bypassActive,
          "requested bypass reopened automatically");
    check(fixture.driver->setBypass(false) == ESP_OK, "request interception rejected");
    fixture.tick();
    check(fixture.driver->_impl->_qualifiedPairs == 0 && !fixture.driver->_impl->_active,
          "leaving bypass reused old qualification");
    fixture.enterIntercept();
    fixture.exchange();
    check(fixture.outgoing()[2] == 0, "leaving bypass replayed cancelled queued command");
    fixture.request();
    fixture.tick(201'000);
    check(fixture.faulted() && fake::pinLevels[GPIO_NUM_15] == 0 &&
          fixture.driver->_impl->_bypassActive, "fault did not restore configured bypass");
    fixture.assertReleased();
  }
  {
    Fixture fixture(false, true, 0);
    check(fake::pinLevels[GPIO_NUM_15] == 1, "inverse bypass polarity not respected");
    fixture.enterIntercept();
    check(fake::pinLevels[GPIO_NUM_15] == 0, "inverse intercept polarity not respected");
  }
  {
    Fixture fixture(false, true);
    fixture.enterIntercept();
    fixture.exchange();
    check(fixture.driver->turn(true) == ESP_OK, "start before transmitted-command bypass test");
    fixture.request();
    check(fixture.driver->setBypass(true) == ESP_OK, "bypass after transmitted start rejected");
    check(fixture.state()["control"]["lastCommandResult"] == "outcome-unconfirmed-no-retry",
          "bypass claimed an already transmitted command was cancelled");
  }
  {
    Fixture fixture(false, true);
    fixture.qualify();
    // Both UARTs previously agreed for three pairs; the fourth frame is
    // complete on one physical tap only and must block interception.
    fake::inject(UART_NUM_1, stockRequest());
    fixture.tick();
    fixture.tick(20'000);
    check(!fixture.driver->_impl->_active && fixture.driver->_impl->_bypassActive,
          "unmatched mirrored packet allowed bypass opening");
  }
  {
    Fixture fixture(false, true);
    fixture.qualify();
    auto corrupt = stockRequest();
    corrupt[6] ^= 1;
    fixture.mirror(corrupt);
    fixture.tick(20'000);
    check(fixture.faulted() && fixture.driver->_impl->_bypassActive,
          "CRC error after qualification allowed interception");
    fixture.assertReleased();
  }
  {
    Fixture fixture(false, true);
    auto changed = stockRequest();
    changed[4] = 18;
    p::updateCrc(changed);
    fake::inject(UART_NUM_1, stockRequest());
    fake::inject(UART_NUM_2, changed);
    fixture.tick();
    check(fixture.faulted() && fixture.driver->_impl->_bypassActive,
          "disagreeing mirrored packets allowed interception");
  }
  {
    Fixture fixture(false, true);
    fixture.mirror(stockRequest());
    fixture.tick(201'000);
    check(fixture.driver->_impl->_qualifiedPairs == 0 &&
          !fixture.driver->_impl->_mirrorWaitingResponse && fixture.driver->_impl->_bypassActive,
          "mirror response timeout did not reset qualification in bypass");
  }
  {
    Fixture fixture(false, true, 1, 1000);
    auto stock = stockRequest();
    stock[0] = 0x76; // Full controller and heater responses share this header.
    p::updateCrc(stock);
    auto misleadingResponse = heaterResponse();
    misleadingResponse[13] = p::ThermostatMode;
    misleadingResponse[17] = 0;
    p::updateCrc(misleadingResponse);
    // Begin observing mid-cycle at a response. Its byte13 happens to look like
    // request mode32. The next request arrives inside the configured 1000ms
    // response timeout, which exposed reversed request/response acquisition.
    for (unsigned i = 0; i < 3; ++i) {
      fixture.mirror(misleadingResponse);
      fixture.mirror(stock);
      if (i < 2) fixture.tick(250'000);
    }
    fixture.tick(20'000);
    check(fixture.driver->_impl->_qualifiedPairs == 2 &&
          fixture.driver->_impl->_mirrorWaitingResponse &&
          fixture.driver->_impl->_bypassActive && !fixture.driver->_impl->_active,
          "reversed response/request phase falsely qualified three pairs");
    fixture.mirror(misleadingResponse);
    check(fixture.driver->_impl->_qualifiedPairs == 3 &&
          fixture.driver->_impl->_stockFrame == stock &&
          fixture.driver->_impl->_heaterFrame == misleadingResponse,
          "phase recovery did not retain correct physical request/response roles");
    fixture.tick(20'000);
    fixture.tick(10'000);
    check(fixture.driver->_impl->_active && !fixture.driver->_impl->_bypassActive,
          "corrected phase failed to qualify interception");
  }
  std::cout << "runtime: optional bypass boot/qualification/polarity/settling, requested latch and fault fallback passed\n";
}

void testConfigurationAndActionValidation() {
  auto rejected = [](auto change, esp_err_t expected) {
    fake::reset();
    Driver::Options options({GPIO_NUM_13, GPIO_NUM_14, UART_NUM_2},
                            {GPIO_NUM_11, GPIO_NUM_12, UART_NUM_1});
    change(options);
    Driver driver("invalid", options);
    check(driver._impl->_lastError == expected && !driver._impl->_task.load(),
          "invalid configuration did not fail before owner launch");
    check(fake::actions.empty(), "invalid configuration altered GPIO ownership");
  };
  rejected([](auto& options) { options.heater.uart = options.display.uart; }, ESP_ERR_INVALID_ARG);
  rejected([](auto& options) { options.heater.txPin = options.display.rxPin; }, ESP_ERR_INVALID_ARG);
  rejected([](auto& options) { options.bypassPin = options.heater.txPin; }, ESP_ERR_INVALID_ARG);
  rejected([](auto& options) { options.fixedPowerMinimumRaw = 15; options.fixedPowerMaximumRaw = 13; }, ESP_ERR_INVALID_ARG);
  rejected([](auto& options) { options.interceptLevel = 2; }, ESP_ERR_INVALID_ARG);
  rejected([](auto&) { fake::uarts[UART_NUM_2].installed = true; }, ESP_ERR_INVALID_STATE);
  {
    Fixture fixture;
    fixture.exchange();
    esp32m::Request request;
    request.payload["on"] = true;
    request.payload["temperatureSetpointC"] = 18;
    esp32m::RequestContext context(request);
    fixture.driver->setState(context);
    check(context.errors.error != ESP_OK && !fixture.driver->_impl->_pendingCommand,
          "multiple heater actions were partially applied");
    request.payload.clear();
    request.payload["temperatureSetpointC"] = 18.5;
    esp32m::RequestContext fractional(request);
    fixture.driver->setState(fractional);
    check(fractional.errors.error != ESP_OK && fixture.state()["configuration"]["authority"] != true,
          "fractional raw wire setpoint silently rounded");
  }
  std::cout << "runtime: GPIO/UART ownership and typed single-action validation passed\n";
}

void testSchedulingAndFirstFaultEvidence() {
  {
    Fixture fixture;
    check(fake::requestedCore == 1 && fake::requestedPriority == 20 &&
          fake::uarts[UART_NUM_1].installCore == 1 && fake::uarts[UART_NUM_2].installCore == 1,
          "UART installation did not follow the dedicated pinned transport owner");
    check(fake::timerStarts == 1 && fake::timerIntervalUs == 1000 &&
          fixture.state()["serial"]["rtosTickUs"].as<int>() == 10000,
          "1ms transport wake timer depends on the project's 10ms RTOS tick");
    fixture.exchange();
    fake::uarts[UART_NUM_2].echo = false;
    fixture.request();
    fake::inject(UART_NUM_2, fixture.outgoing());
    fixture.tick(12'000);
    check(!fixture.faulted() && !fixture.driver->_impl->_sides[1].echoPending,
          "bounded delayed buffered echo was not consumed exactly");
    fixture.reply();
  }
  check(!fake::ownerExitedWithActiveTimer && fake::timerStops == 1 &&
        fake::timerDeletes == 1, "owner exited while timer callback could notify a freed task");
  {
    Fixture fixture;
    fixture.exchange();
    fake::uarts[UART_NUM_2].echo = false;
    fixture.request();
    auto wrongEcho = fixture.outgoing();
    wrongEcho[0] ^= 1;
    fake::inject(UART_NUM_2, wrongEcho);
    fake::actions.clear();
    fixture.tick(25'000);
    const auto report = fixture.state();
    const auto detail = report["serial"]["faultDetail"];
    check(fixture.faulted() && detail["kind"] == "echo-mismatch" &&
          detail["side"] == "heater" &&
          detail["expectedByte"].as<int>() != detail["receivedByte"].as<int>() &&
          detail["workerGapUs"].as<uint64_t>() == 25'000,
          "late wrong echo byte lost its mismatch evidence");
    check(report["serial"]["workerMaxGapUs"].as<uint64_t>() >= 25'000,
          "transport scheduling gap was not retained");
    check(!fake::actions.empty() && fake::actions.front() == "release:12",
          "fault snapshot queried UART before releasing transmitters");
    std::string originalDetail;
    serializeJson(detail, originalDetail);
    const auto originalFault = fixture.driver->_impl->_fault;
    for (unsigned i = 0; i < 3; ++i) {
      fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(), 512, 0x76);
      fake::uarts[UART_NUM_2].rx.insert(fake::uarts[UART_NUM_2].rx.end(), 512, 0x16);
      fake::uarts[UART_NUM_1].events.push_back({UART_BUFFER_FULL, 0, false});
      fake::uarts[UART_NUM_2].events.push_back({UART_FRAME_ERR, 0, false});
      fixture.tick();
      check(fake::uarts[UART_NUM_1].rx.empty() && fake::uarts[UART_NUM_2].rx.empty() &&
            fake::uarts[UART_NUM_1].events.empty() && fake::uarts[UART_NUM_2].events.empty(),
            "latched bridge stopped draining continuing stock traffic");
      std::string retained;
      serializeJson(fixture.state()["serial"]["faultDetail"], retained);
      check(fixture.driver->_impl->_fault == originalFault && retained == originalDetail,
            "secondary UART fault overwrote initiating echo evidence");
    }
    fixture.rearm();
    check(fixture.state()["serial"]["faultDetail"].isNull(),
          "explicit rearm retained stale initiating-fault snapshot");
    fake::uarts[UART_NUM_2].echo = true;
    fixture.exchange();
    check(fixture.outgoing()[2] == 0, "fault evidence rearm replayed a command");
  }
  const std::array<std::pair<int, const char*>, 5> uartFaults{{
      {UART_FIFO_OVF, "uart-fifo-overflow"}, {UART_BUFFER_FULL, "uart-buffer-full"},
      {UART_FRAME_ERR, "uart-frame-error"}, {UART_PARITY_ERR, "uart-parity-error"},
      {UART_BREAK, "uart-break"}}};
  for (const auto& fault : uartFaults) {
    Fixture fixture;
    fixture.exchange();
    fake::uarts[UART_NUM_2].events.push_back({fault.first, 0, false});
    fake::uarts[UART_NUM_2].events.push_back({UART_DATA, 1, false});
    fixture.tick();
    const auto report = fixture.state();
    check(report["serial"]["faultDetail"]["kind"] == fault.second &&
          report["serial"]["faultDetail"]["side"] == "heater" &&
          report["serial"]["faultDetail"]["queuedEvents"].as<unsigned>() == 1,
          "distinct UART event type/endpoint/queue evidence was lost");
    fixture.assertReleased();
  }
  for (unsigned failure = 0; failure < 4; ++failure) {
    fake::reset();
    if (failure == 0) fake::failParamUart = UART_NUM_1;
    if (failure == 1) fake::failInstallUart = UART_NUM_2;
    if (failure == 2) fake::failCreateTimer = true;
    if (failure == 3) fake::failCreateTask = true;
    {
      Driver::Options options({GPIO_NUM_13, GPIO_NUM_14, UART_NUM_2},
                              {GPIO_NUM_11, GPIO_NUM_12, UART_NUM_1});
      options.bypassPin = GPIO_NUM_15;
      Driver driver("initialization-failure", options);
      check(!driver._impl->_fault.empty() && !driver._impl->_task.load() &&
            fake::pinModes[GPIO_NUM_12] == GPIO_MODE_INPUT &&
            fake::pinModes[GPIO_NUM_14] == GPIO_MODE_INPUT && fake::pinLevels[GPIO_NUM_15] == 0,
            "partial hardware/timer/task startup failure did not retain released bypass");
    }
    check(!fake::uarts[UART_NUM_1].installed && !fake::uarts[UART_NUM_2].installed,
          "failed startup destruction leaked a UART driver");
    if (failure != 2)
      check(fake::timerStops == 1 && fake::timerDeletes == 1,
            "failed startup destruction leaked the transport timer");
  }
  std::cout << "runtime: pinned initialization, tick-independent wake, delayed echo, sticky first fault and failed-start cleanup passed\n";
}

void testBufferedEchoAndCorrelationBudget() {
  auto prepareDisplayTail = [](Fixture& fixture, size_t prefix = 19) {
    fixture.exchange();
    fake::uarts[UART_NUM_1].echo = false;
    fixture.request();
    fake::inject(UART_NUM_2, heaterResponse());
    fixture.tick(); // Submit the heater response to the physical display.
    const auto frame = heaterResponse();
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(),
                                    frame.begin(), frame.begin() + prefix);
    fixture.tick();
    check(fixture.driver->_impl->_sides[0].echoPosition == prefix &&
          fixture.driver->_impl->_sides[0].echoPending == p::FrameSize - prefix,
          "recorded echo-tail boundary was not established");
    return frame;
  };
  auto finishBufferedEcho = [](Fixture& fixture) {
    fake::afterUartRead = {};
    // Expiration requires a nonblocking read that observed no bytes after the
    // deadline. The first pass may only drain the late tail; the next empty
    // pass distinguishes an absent byte from a pause inside the earlier read.
    for (unsigned i = 0; i < 3 && !fixture.faulted() &&
         fixture.driver->_impl->_sides[0].echoPending; ++i) fixture.tick();
  };
  {
    Fixture fixture;
    const auto frame = prepareDisplayTail(fixture);
    const auto heaterTx = fake::uarts[UART_NUM_2].submitted.size();
    const auto displayTx = fake::uarts[UART_NUM_1].submitted.size();
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(),
                                    frame.begin() + 19, frame.end());
    fixture.tick(24'257);
    const auto report = fixture.state();
    check(!fixture.faulted() && !fixture.driver->_impl->_sides[0].echoPending &&
          fake::uarts[UART_NUM_2].submitted.size() == heaterTx &&
          fake::uarts[UART_NUM_1].submitted.size() == displayTx,
          "exact buffered tail serviced after a scheduling stall faulted or forwarded its echo");
    check(report["serial"]["echoDelayedReads"].as<unsigned>() >= 1 &&
          report["serial"]["echoMaxServiceDelayUs"].as<uint64_t>() > 0,
          "accepted delayed echo did not retain service-latency diagnostics");
  }
  {
    Fixture fixture;
    const auto frame = prepareDisplayTail(fixture, 17);
    const auto heaterTx = fake::uarts[UART_NUM_2].submitted.size();
    const auto displayTx = fake::uarts[UART_NUM_1].submitted.size();
    bool injected = false;
    fake::afterUartRead = [&](uart_port_t uart) {
      if (uart != UART_NUM_2 || injected) return;
      injected = true;
      // A cache/scheduling pause can occur inside a step, after display RX was
      // already drained. Its exact echo tail arrives during that pause.
      fake::nowUs += 25'000;
      auto& display = fake::uarts[UART_NUM_1];
      display.rx.insert(display.rx.end(), frame.begin() + 17, frame.end());
      display.events.push_back({UART_DATA, 7, false});
    };
    fixture.tick();
    const auto workerGapAtPause = fixture.driver->_impl->_workerGapUs;
    finishBufferedEcho(fixture);
    const auto report = fixture.state();
    check(injected && !fixture.faulted() &&
          !fixture.driver->_impl->_sides[0].echoPending &&
          fake::uarts[UART_NUM_1].rx.empty() &&
          fake::uarts[UART_NUM_2].submitted.size() == heaterTx &&
          fake::uarts[UART_NUM_1].submitted.size() == displayTx,
          "exact echo queued during a pause inside step expired after the earlier display drain");
    check(workerGapAtPause == 1000 &&
          report["serial"]["echoDelayedReads"].as<unsigned>() >= 1,
          "mid-step pause was confused with an inter-step worker gap or lost delayed-read evidence");
  }
  {
    Fixture fixture;
    auto frame = prepareDisplayTail(fixture, 17);
    frame[17] ^= 1;
    bool injected = false;
    fake::afterUartRead = [&](uart_port_t uart) {
      if (uart != UART_NUM_2 || injected) return;
      injected = true;
      fake::nowUs += 25'000;
      auto& display = fake::uarts[UART_NUM_1];
      display.rx.insert(display.rx.end(), frame.begin() + 17, frame.end());
      display.events.push_back({UART_DATA, 7, false});
    };
    fixture.tick();
    finishBufferedEcho(fixture);
    const auto report = fixture.state();
    const auto detail = report["serial"]["faultDetail"];
    check(injected && fixture.faulted() && detail["kind"] == "echo-mismatch" &&
          detail["echoPosition"].as<int>() == 17 &&
          detail["receivedByte"].as<int>() == frame[17],
          "incorrect tail queued during a mid-step pause lost exact mismatch evidence");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    const auto frame = prepareDisplayTail(fixture, 17);
    bool injected = false;
    fake::afterUartRead = [&](uart_port_t uart) {
      if (uart != UART_NUM_2 || injected) return;
      injected = true;
      fake::nowUs += 25'000;
      auto& display = fake::uarts[UART_NUM_1];
      display.rx.insert(display.rx.end(), frame.begin() + 17, frame.end() - 1);
      display.events.push_back({UART_DATA, 6, false});
    };
    fixture.tick();
    finishBufferedEcho(fixture);
    const auto report = fixture.state();
    const auto detail = report["serial"]["faultDetail"];
    check(injected && fixture.faulted() && detail["kind"] == "echo-missing" &&
          detail["echoPosition"].as<int>() == 23 &&
          detail["echoPending"].as<int>() == 1 &&
          detail["rxReadBytes"].as<int>() == 0 &&
          detail["rxReadAtUs"].as<uint64_t>() > detail["echoDeadlineUs"].as<uint64_t>(),
          "mid-step pause recovery hid a truly absent final echo byte");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    const auto frame = prepareDisplayTail(fixture, 17);
    bool injected = false;
    fake::afterUartRead = [&](uart_port_t uart) {
      if (uart != UART_NUM_2 || injected) return;
      injected = true;
      fake::nowUs += 25'000;
      auto& display = fake::uarts[UART_NUM_1];
      display.rx.insert(display.rx.end(), frame.begin() + 17, frame.end());
      display.events.push_back({UART_FRAME_ERR, 0, false});
    };
    fixture.tick();
    finishBufferedEcho(fixture);
    const auto report = fixture.state();
    check(injected && fixture.faulted() &&
          report["serial"]["faultDetail"]["kind"] == "uart-frame-error",
          "mid-step echo recovery skipped a newly queued UART error");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    auto frame = prepareDisplayTail(fixture);
    frame[19] ^= 1;
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(),
                                    frame.begin() + 19, frame.end());
    fixture.tick(24'257);
    check(fixture.faulted() && fixture.state()["serial"]["faultDetail"]["kind"] == "echo-mismatch" &&
          fixture.state()["serial"]["faultDetail"]["echoPosition"].as<int>() == 19,
          "late mismatching tail was accepted as scheduling delay");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    const auto frame = prepareDisplayTail(fixture);
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(),
                                    frame.begin() + 19, frame.end() - 1);
    fixture.tick(24'257);
    finishBufferedEcho(fixture);
    check(fixture.faulted() && fixture.state()["serial"]["faultDetail"]["kind"] == "echo-missing" &&
          fixture.state()["serial"]["faultDetail"]["echoPending"].as<int>() == 1,
          "missing final echo byte was hidden by drain-before-expire policy");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    fixture.exchange();
    check(fixture.driver->turn(true) == ESP_OK, "queue start before service-stall guard test");
    const auto heaterTx = fake::uarts[UART_NUM_2].submitted.size();
    fake::inject(UART_NUM_1, stockRequest());
    fixture.tick(201'000);
    check(fixture.faulted() && fixture.state()["serial"]["faultDetail"]["kind"] == "worker-stall" &&
          fake::uarts[UART_NUM_2].submitted.size() == heaterTx,
          "unbounded service stall forwarded a stale queued request/command");
    fixture.rearm();
    fixture.exchange();
    check(fixture.outgoing()[2] == 0, "service-stall rearm replayed queued start");
  }
  {
    Fixture fixture;
    fixture.exchange();
    check(fixture.driver->turn(true) == ESP_OK, "queue start before mid-step stall guard test");
    const auto frame = stockRequest();
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(),
                                    frame.begin(), frame.begin() + 17);
    fixture.tick();
    const auto heaterTx = fake::uarts[UART_NUM_2].submitted.size();
    bool injected = false;
    fake::afterUartRead = [&](uart_port_t uart) {
      if (uart != UART_NUM_2 || injected) return;
      injected = true;
      fake::nowUs += 201'000;
      auto& display = fake::uarts[UART_NUM_1];
      display.rx.insert(display.rx.end(), frame.begin() + 17, frame.end());
      display.events.push_back({UART_DATA, 7, false});
    };
    fixture.tick();
    fake::afterUartRead = {};
    check(fake::uarts[UART_NUM_2].submitted.size() == heaterTx,
          "mid-step outage submitted a command before the next owner iteration");
    if (!fixture.faulted()) fixture.tick();
    check(injected && fixture.faulted() &&
          fake::uarts[UART_NUM_2].submitted.size() == heaterTx,
          "mid-step outage forwarded a stale queued request or one-shot command");
    fixture.rearm();
    fixture.exchange();
    check(fixture.outgoing()[2] == 0, "mid-step outage rearm replayed queued start");
  }
  {
    Fixture fixture;
    fixture.exchange();
    fixture.request();
    fixture.tick(150'000);
    const auto displayTx = fake::uarts[UART_NUM_1].submitted.size();
    fake::inject(UART_NUM_2, heaterResponse(5, 1));
    fixture.tick(60'000); // Total transaction age210ms, latest worker gap60ms.
    check(fixture.faulted() && fake::uarts[UART_NUM_1].submitted.size() == displayTx &&
          fixture.driver->_impl->_response.runState == 0,
          "buffered reply past transaction deadline was treated as fresh and forwarded");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    fixture.exchange();
    check(fixture.driver->turn(true) == ESP_OK, "queue start before stalled request processing test");
    const auto heaterTx = fake::uarts[UART_NUM_2].submitted.size();
    fake::inject(UART_NUM_1, stockRequest());
    bool injected = false;
    fake::afterUartRead = [&](uart_port_t uart) {
      if (uart != UART_NUM_1 || injected) return;
      injected = true;
      // The complete packet was copied by the UART read, but processing its
      // bytes resumes only after the bounded correlation budget is exhausted.
      fake::nowUs += 201'000;
    };
    fixture.tick();
    fake::afterUartRead = {};
    check(injected && fixture.faulted() &&
          fake::uarts[UART_NUM_2].submitted.size() == heaterTx,
          "request copied before a long mid-step pause submitted a stale Start");
    const auto report = fixture.state();
    check(report["serial"]["faultDetail"]["kind"] == "worker-stall" &&
          report["serial"]["faultDetail"]["stepAgeUs"].as<uint64_t>() == 201'000 &&
          report["serial"]["faultDetail"]["workerGapUs"].as<uint64_t>() == 1000,
          "mid-step stall evidence confused processing duration with the entry gap");
    fixture.rearm();
    fixture.exchange();
    check(fixture.outgoing()[2] == 0, "stalled request processing rearm replayed queued start");
  }
  {
    Fixture fixture;
    fixture.exchange();
    fixture.request();
    fixture.tick(150'000);
    const auto displayTx = fake::uarts[UART_NUM_1].submitted.size();
    fake::inject(UART_NUM_2, heaterResponse(5, 1));
    bool injected = false;
    fake::afterUartRead = [&](uart_port_t uart) {
      if (uart != UART_NUM_2 || injected) return;
      injected = true;
      fake::nowUs += 60'000;
    };
    fixture.tick();
    fake::afterUartRead = {};
    check(injected && fixture.faulted() &&
          fake::uarts[UART_NUM_1].submitted.size() == displayTx &&
          fixture.driver->_impl->_response.runState == 0,
          "reply copied before a mid-step pause became fresh after its response deadline");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    fixture.exchange();
    check(fixture.driver->turn(true) == ESP_OK,
          "queue start before response-send preflight pause test");
    fixture.request();
    fixture.tick(150'000);
    const auto displayTx = fake::uarts[UART_NUM_1].submitted.size();
    const auto previousHeaterStamp = fixture.driver->_impl->_heaterStamp;
    fake::inject(UART_NUM_2, heaterResponse(5, 1));
    bool injected = false;
    fake::afterBufferedDataQuery = [&](uart_port_t uart) {
      if (uart != UART_NUM_1 || injected) return;
      injected = true;
      // Response parsing is timely. A pause while checking the destination
      // UART crosses the original transaction deadline before FIFO submission.
      fake::nowUs += 60'000;
    };
    fixture.tick();
    fake::afterBufferedDataQuery = {};
    check(injected && fixture.faulted() &&
          fake::uarts[UART_NUM_1].submitted.size() == displayTx &&
          fixture.driver->_impl->_response.runState == 0 &&
          fixture.driver->_impl->_heaterStamp == previousHeaterStamp,
          "response preflight pause forwarded or published a reply after its original deadline");
    check(fixture.state()["control"]["lastCommandResult"] == "uncertain-no-retry",
          "expired response preflight falsely resolved an outstanding Start observation");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    fixture.exchange();
    const auto frame = stockRequest();
    const auto heaterTx = fake::uarts[UART_NUM_2].submitted.size();
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(), frame.begin(), frame.begin() + 19);
    fixture.tick();
    check(fixture.driver->_impl->_sides[0].parser.buffered() == 19,
          "partial protocol frame boundary was not established");
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(), frame.begin() + 19, frame.end());
    fixture.tick(24'257);
    check(!fixture.faulted() && fixture.outgoing() == frame &&
          fake::uarts[UART_NUM_2].submitted.size() == heaterTx + 1,
          "queued CRC-valid frame tail was expired before draining or forwarded twice");
    fixture.reply();
  }
  {
    Fixture fixture;
    fixture.exchange();
    const auto frame = stockRequest();
    const auto heaterTx = fake::uarts[UART_NUM_2].submitted.size();
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(),
                                    frame.begin(), frame.begin() + 17);
    fixture.tick();
    bool injected = false;
    fake::afterUartRead = [&](uart_port_t uart) {
      if (uart != UART_NUM_2 || injected) return;
      injected = true;
      fake::nowUs += 25'000;
      auto& display = fake::uarts[UART_NUM_1];
      display.rx.insert(display.rx.end(), frame.begin() + 17, frame.end());
      display.events.push_back({UART_DATA, 7, false});
    };
    fixture.tick();
    fake::afterUartRead = {};
    check(injected && !fixture.faulted(),
          "partial frame expired while its tail arrived during a mid-step pause");
    fixture.tick();
    check(!fixture.faulted() && fixture.outgoing() == frame &&
          fake::uarts[UART_NUM_2].submitted.size() == heaterTx + 1,
          "CRC-valid tail queued during a mid-step pause was not forwarded exactly once");
    fixture.reply();
  }
  {
    Fixture fixture;
    fixture.exchange();
    auto frame = stockRequest();
    const auto heaterTx = fake::uarts[UART_NUM_2].submitted.size();
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(), frame.begin(), frame.begin() + 19);
    fixture.tick();
    frame.back() ^= 1;
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(), frame.begin() + 19, frame.end());
    fixture.tick(24'257);
    check(fixture.faulted() && fake::uarts[UART_NUM_2].submitted.size() == heaterTx &&
          fixture.state()["serial"]["crcErrors"].as<int>() > 0,
          "corrupt delayed frame tail was accepted or forwarded");
    fixture.assertReleased();
  }
  {
    Fixture fixture;
    fixture.exchange();
    const auto frame = stockRequest();
    const auto heaterTx = fake::uarts[UART_NUM_2].submitted.size();
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(), frame.begin(), frame.begin() + 19);
    fixture.tick();
    fake::uarts[UART_NUM_1].rx.insert(fake::uarts[UART_NUM_1].rx.end(), frame.begin() + 19, frame.end() - 1);
    fixture.tick(24'257);
    check(!fixture.faulted(), "recent incomplete tail expired before its remaining-byte window");
    fixture.tick(21'000);
    check(fixture.faulted() && fake::uarts[UART_NUM_2].submitted.size() == heaterTx,
          "missing final frame byte escaped explicit post-drain timeout");
    fixture.assertReleased();
  }
  std::cout << "runtime: queued echo/frame drain before expiry, strict mismatch/missing/CRC and bounded correlation passed\n";
}

p::Frame fullyStoppedResponse() {
  auto response = heaterResponse();
  response[6] = response[7] = response[16] = 0;
  p::updateCrc(response);
  return response;
}

esp_err_t configure(Fixture& fixture, const char* mode, int value) {
  Driver::Configuration configuration;
  configuration.mode = std::strcmp(mode, "power") == 0 ? Driver::Mode::FixedPower
                                                       : Driver::Mode::Temperature;
  configuration.value = value;
  return fixture.driver->setConfiguration(configuration);
}

void populateConfiguration(JsonObject object, const JsonDocument& state,
                           const char* mode = "temperature", int value = 18) {
  (void)state;
  object["mode"] = mode;
  object[std::strcmp(mode, "power") == 0 ? "fixedPowerSettingRaw"
                                       : "temperatureSetpointC"] = value;
}

p::Frame qualifyingServiceRequest() {
  auto result = stockRequest();
  result[0] = 0x76;
  p::updateCrc(result);
  return result;
}

p::ServiceParameters capturedService() {
  p::ServiceParameters result;
  check(p::decodeService(stockRequest(), result), "test service fixture invalid");
  return result;
}

p::ServiceParameters selectedService() {
  return {15, 56, 1600, 4600, 120, 2, 6};
}

void populateService(JsonObject object, const p::ServiceParameters& service) {
  object["minimumPumpHz"] = service.minimumPumpDecihz / 10.0;
  object["maximumPumpHz"] = service.maximumPumpDecihz / 10.0;
  object["minimumFanRpm"] = service.minimumFanRpm;
  object["maximumFanRpm"] = service.maximumFanRpm;
  object["configuredVoltageV"] = service.configuredVoltageDeciv / 10.0;
  object["hallCount"] = service.fanHallPulses;
  object["glowPowerLevel"] = service.glowPower;
}

void populateServiceConfig(esp32m::Request& request, const JsonDocument& state,
                           const p::ServiceParameters& service = selectedService()) {
  (void)state;
  populateService(request.payload["service"].to<JsonObject>(), service);
}

bool restoreService(Fixture& fixture, const p::ServiceParameters& service = selectedService()) {
  esp32m::Request request;
  request.internal = true;
  populateService(request.payload["service"].to<JsonObject>(), service);
  esp32m::RequestContext context(request);
  const bool changed = fixture.driver->setConfig(context);
  check(context.errors.error == ESP_OK, "persisted internal service restore rejected");
  return changed;
}

bool saveService(Fixture& fixture, const p::ServiceParameters& service = selectedService()) {
  esp32m::Request request;
  populateServiceConfig(request, fixture.state(), service);
  esp32m::RequestContext context(request);
  const bool changed = fixture.driver->setConfig(context);
  check(context.errors.error == ESP_OK, "fresh guarded service config rejected");
  return changed;
}

JsonDocument persistedConfig(Fixture& fixture) {
  esp32m::Request request;
  request.internal = true;
  esp32m::RequestContext context(request);
  std::unique_ptr<JsonDocument> document(fixture.driver->getConfig(context));
  check(document != nullptr, "getConfig omitted persistent document");
  JsonDocument result;
  result.set(*document);
  return result;
}

void checkService(JsonVariantConst object, const p::ServiceParameters& service,
                  const char* message) {
  check(object.is<JsonObjectConst>() && object.size() == 7 &&
        std::abs(object["minimumPumpHz"].as<double>() - service.minimumPumpDecihz / 10.0) < 0.00001 &&
        std::abs(object["maximumPumpHz"].as<double>() - service.maximumPumpDecihz / 10.0) < 0.00001 &&
        object["minimumFanRpm"] == service.minimumFanRpm &&
        object["maximumFanRpm"] == service.maximumFanRpm &&
        std::abs(object["configuredVoltageV"].as<double>() - service.configuredVoltageDeciv / 10.0) < 0.00001 &&
        object["hallCount"] == service.fanHallPulses && object["glowPowerLevel"] == service.glowPower,
        message);
}

p::Frame withService(p::Frame frame, const p::ServiceParameters& service = selectedService()) {
  check(p::patchService(frame, service), "test overlay patch rejected");
  return frame;
}

void populateOperating(JsonObject object, const char* mode = "temperature",
                       int value = 18) {
  object["mode"] = mode;
  object[std::strcmp(mode, "power") == 0 ? "fixedPowerSettingRaw"
                                        : "temperatureSetpointC"] = value;
}

bool restoreOperating(Fixture& fixture, const char* mode = "temperature", int value = 18) {
  esp32m::Request request;
  request.internal = true;
  populateOperating(request.payload["operating"].to<JsonObject>(), mode, value);
  esp32m::RequestContext context(request);
  const bool changed = fixture.driver->setConfig(context);
  check(context.errors.error == ESP_OK, "persisted internal operating restore rejected");
  return changed;
}

void populateOperatingConfig(esp32m::Request& request, const JsonDocument& state,
                             const char* mode = "temperature", int value = 18) {
  (void)state;
  populateOperating(request.payload["operating"].to<JsonObject>(), mode, value);
}

p::Frame withOperating(p::Frame frame, const char* mode = "temperature", int value = 18,
                        int ambient = 22) {
  check(p::patchOperating(frame, std::strcmp(mode, "power") == 0 ? p::FixedPowerMode
                                                               : p::ThermostatMode,
                          value, ambient), "test persistent operating patch rejected");
  return frame;
}

void testModeConfigurationAtomicityAndState() {
  Fixture fixture(true);
  check(configure(fixture, "power", 13) == ESP_ERR_INVALID_STATE,
        "operating settings accepted before fresh paired traffic");
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  check(configure(fixture, "power", 13) == ESP_OK,
        "fresh stopped persistent power configuration rejected");
  const auto stored = persistedConfig(fixture);
  check(stored["operating"]["mode"] == "power" &&
        stored["operating"]["fixedPowerSettingRaw"] == 13 &&
        stored["operating"]["temperatureSetpointC"].isUnbound(),
        "selected operating setting did not persist or invented an unobserved temperature");
  check(fixture.state()["configuration"]["leaseRemainingMs"].isUnbound() &&
        fixture.state()["configuration"]["maximumLeaseMs"].isUnbound(),
        "production configuration still exposes a finite lease");
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(stockRequest(), "power", 13),
        "persistent mode/value failed atomic wire substitution");
  const auto active = fixture.state();
  check(active["display"]["mode"] == "temperature" &&
        active["configuration"]["stockMode"] == "temperature" &&
        active["configuration"]["mode"] == "power" &&
        active["configuration"]["value"] == 13 &&
        active["configuration"]["submittedMode"] == "power" &&
        active["configuration"]["submittedValue"] == 13 &&
        active["configuration"]["authority"] == true &&
        active["configuration"]["active"] == true,
        "stock, desired, submitted and authority state were conflated");
  const auto response = fullyStoppedResponse();
  check(fake::uarts[UART_NUM_1].submitted.back() ==
        std::vector<uint8_t>(response.begin(), response.end()),
        "persistent operating config modified a heater reply");
  for (unsigned halfSecond = 0; halfSecond < 140; ++halfSecond) {
    for (unsigned millisecond = 0; millisecond < 500; ++millisecond) fixture.tick();
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    check(!fixture.faulted(), "persistent-duration test stopped servicing the UART owner");
  }
  check(fixture.outgoing() == withOperating(stockRequest(), "power", 13) &&
        fixture.state()["configuration"]["authority"] == true,
        "persistent production mode expired after the former maximum lease");
  check(fixture.driver->releaseOverride() == ESP_OK, "release action rejected");
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(stockRequest(), "power", 13),
        "research/legacy release discarded persistent production authority");
  const auto replacement = configure(fixture, "temperature", 18);
  if (replacement != ESP_OK) {
    std::string diagnostic;
    serializeJson(fixture.state(), diagnostic);
    std::cerr << "persistent replacement error=" << replacement << " state=" << diagnostic << '\n';
  }
  check(replacement == ESP_OK,
        "persistent replacement by thermostat configuration rejected");
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(stockRequest()), "persistent thermostat patch differed");
  const auto rememberedConfig = persistedConfig(fixture);
  const auto remembered = rememberedConfig["operating"];
  check(remembered["mode"] == "temperature" && remembered["temperatureSetpointC"] == 18 &&
        remembered["fixedPowerSettingRaw"] == 13,
        "changing selected mode forgot the other mode's saved setpoint");
  check(fixture.driver->turn(true) == ESP_OK,
        "persistent operating authority blocked an explicit Start");
  fixture.request();
  auto command = withOperating(stockRequest());
  p::patchCommand(command, p::StartCommand);
  check(fixture.outgoing() == command, "persistent operating patch stripped Start");
  fixture.reply(heaterResponse(1, 1));
  check(fixture.driver->turn(false) == ESP_OK,
        "persistent operating authority blocked an explicit Stop");
  fixture.request();
  command = withOperating(stockRequest());
  p::patchCommand(command, p::StopCommand);
  check(fixture.outgoing() == command, "persistent operating patch stripped Stop");
  fixture.reply(heaterResponse(6, 1));
  check(persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18,
        "Stop discarded persistent operating settings");
  std::cout << "configuration: indefinite atomic mode/value, remembered setpoints, source state and command preservation passed\n";
}

void testModeConfigurationEligibilityAndAmbient() {
  {
    Fixture fixture(true);
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    fixture.request();
    check(fixture.state()["configuration"]["canApply"] == true &&
          configure(fixture, "temperature", 18) == ESP_OK && fixture.outgoing() == stockRequest(),
          "transient request/echo state flickered availability or rejected a pending operating edit");
    fixture.reply(fullyStoppedResponse());
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == withOperating(stockRequest()),
          "mid-transaction operating edit did not apply at the next physical request");
    fake::nowUs += 2'000'001;
    check(configure(fixture, "temperature", 18) == ESP_ERR_INVALID_STATE,
          "operating edit accepted stale telemetry");
  }
  {
    Fixture fixture(true);
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    check(configure(fixture, "power", 13) == ESP_OK, "owned-mode eligibility setup failed");
    fixture.exchange(stockRequest(), heaterResponse(5, 1));
    check(configure(fixture, "power", 14) == ESP_OK,
          "same owned-mode running setpoint compared against stale physical display mode");
    check(configure(fixture, "temperature", 18) == ESP_ERR_INVALID_STATE,
          "running edit changed the driver's owned mode");
    fixture.exchange(stockRequest(), heaterResponse(5, 1));
    check(fixture.outgoing() == withOperating(stockRequest(), "power", 14),
          "healthy-running owned setpoint was not substituted");
  }
  for (const auto phase : {std::array<uint8_t, 2>{1, 1}, {6, 1}, {8, 0}, {0, 10}}) {
    Fixture fixture(true);
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    check(configure(fixture, "temperature", 18) == ESP_OK, "runtime phase setup rejected");
    fixture.exchange(stockRequest(), heaterResponse(phase[0], phase[1]));
    check(fixture.outgoing() == withOperating(stockRequest()) &&
          fixture.state()["configuration"]["authority"] == true,
          "startup/cooldown/error telemetry discarded persistent operating settings");
    check(configure(fixture, "temperature", 19) == ESP_ERR_INVALID_STATE,
          "startup/cooldown/error phase accepted an operating edit");
  }
  {
    Fixture fixture(true);
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    check(configure(fixture, "temperature", 18) == ESP_OK, "ambient fallback setup rejected");
    auto physicalPower = stockRequest();
    physicalPower[3] = 0;
    physicalPower[4] = 14;
    physicalPower[13] = p::FixedPowerMode;
    p::updateCrc(physicalPower);
    fixture.exchange(physicalPower, fullyStoppedResponse());
    const auto suspendedState = fixture.state();
    const auto suspended = suspendedState["configuration"];
    check(fixture.outgoing() == physicalPower && suspended["authority"] == true &&
          suspended["mode"] == "temperature" && suspended["value"] == 18 &&
          suspended["active"] == false && suspended["phase"] == "suspended",
          "power ambient placeholder did not suspend without forgetting desired thermostat settings");
    auto zero = stockRequest();
    zero[3] = 0;
    p::updateCrc(zero);
    fixture.exchange(zero, fullyStoppedResponse());
    check(fixture.outgoing() == withOperating(zero, "temperature", 18, 0) &&
          fixture.state()["configuration"]["active"] == true,
          "fresh measured thermostat zero did not automatically resume authority");
    auto negative = stockRequest();
    negative[3] = 0xff;
    p::updateCrc(negative);
    fixture.exchange(negative, fullyStoppedResponse());
    check(fixture.outgoing() == negative && persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18,
          "unsupported negative ambient cleared desired temperature or fabricated a sensor value");
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == withOperating(stockRequest()),
          "valid ambient did not resume suspended persistent mode");
  }
  {
    Fixture fixture;
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    check(configure(fixture, "power", 13) != ESP_OK,
          "unconfigured power range enabled persistent power edits");
  }
  {
    Fixture fixture(true);
    fixture.driver->_impl->_options.requireFixedPowerTemplate = true;
    fixture.driver->_impl->_options.fixedPowerTemplate = stockRequest();
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    check(configure(fixture, "power", 13) == ESP_OK, "qualified effective power profile rejected");
    auto changed = stockRequest();
    ++changed[5];
    p::updateCrc(changed);
    fixture.exchange(changed, fullyStoppedResponse());
    check(configure(fixture, "power", 14) != ESP_OK &&
          persistedConfig(fixture)["operating"]["fixedPowerSettingRaw"] == 13,
          "unqualified profile edited or erased saved raw power");
  }
  std::cout << "configuration: owned-mode running eligibility, persistent startup behavior and ambient fallback/resume passed\n";
}

void testModeConfigurationGuardsAndJson() {
  Fixture fixture(true);
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  auto reject = [&](auto mutate) {
    esp32m::Request request;
    populateConfiguration(request.payload["configuration"].to<JsonObject>(), fixture.state());
    mutate(request);
    esp32m::RequestContext context(request);
    fixture.driver->setState(context);
    check(context.errors.error != ESP_OK && persistedConfig(fixture)["operating"].isUnbound(),
          "invalid operating action accepted or partially persisted");
  };
  reject([](auto& r) { r.payload["configuration"]["unknown"] = 1; });
  reject([](auto& r) { r.payload["configuration"]["service"]["minimumPumpHz"] = 1.5; });
  reject([](auto& r) { r.payload["configuration"]["fixedPowerSettingRaw"] = 13; });
  reject([](auto& r) { r.payload["configuration"]["temperatureSetpointC"] = 18.5; });
  reject([](auto& r) { r.payload["configuration"]["temperatureSetpointC"] = true; });
  reject([](auto& r) { r.payload["configuration"]["temperatureSetpointC"] = nullptr; });
  reject([](auto& r) { r.payload["configuration"]["mode"] = std::string("temperature\0power", 17); });
  reject([](auto& r) { r.payload["configuration"]["leaseMs"] = 60000; });
  reject([](auto& r) { r.broadcast = true; });
  esp32m::Request accepted;
  populateConfiguration(accepted.payload["configuration"].to<JsonObject>(), fixture.state());
  esp32m::RequestContext context(accepted);
  const auto events = fake::configEvents;
  fixture.driver->setState(context);
  check(context.errors.error == ESP_OK &&
        persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18 &&
        fake::configEvents == events + 1 && !fake::configPublishedInOwner,
        "guarded operating action did not persist/publish after unlocking");
  esp32m::RequestContext replay(accepted);
  fixture.driver->setState(replay);
  check(replay.errors.error == ESP_OK && fake::configEvents == events + 1,
        "identical permanent action required a reload or repeated persistence events");
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  esp32m::Request combined;
  populateOperatingConfig(combined, fixture.state(), "power", 14);
  combined.payload["operating"]["temperatureSetpointC"] = 19;
  populateService(combined.payload["service"].to<JsonObject>(), selectedService());
  esp32m::RequestContext combinedContext(combined);
  check(fixture.driver->setConfig(combinedContext) && combinedContext.errors.error == ESP_OK,
        "atomic service and operating save rejected");
  auto saved = persistedConfig(fixture);
  check(saved["operating"]["mode"] == "power" &&
        saved["operating"]["fixedPowerSettingRaw"] == 14 &&
        saved["operating"]["temperatureSetpointC"] == 19,
        "atomic operating save lost selected or remembered values");
  checkService(saved["service"], selectedService(), "combined save lost service values");
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  esp32m::Request invalidCombined;
  populateOperatingConfig(invalidCombined, fixture.state(), "temperature", 20);
  populateService(invalidCombined.payload["service"].to<JsonObject>(), selectedService());
  invalidCombined.payload["service"]["minimumFanRpm"] = 1601;
  esp32m::RequestContext invalidContext(invalidCombined);
  check(!fixture.driver->setConfig(invalidContext) && invalidContext.errors.error != ESP_OK &&
        persistedConfig(fixture)["operating"]["mode"] == "power" &&
        persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 19,
        "invalid service half partially committed an operating update");
  std::cout << "configuration: strict explicit permanent actions, no reload prerequisite and atomic service/operating saves passed\n";
}

void testPersistentConfigurationPhysicalEditsAndFault() {
  Fixture fixture(true);
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  check(configure(fixture, "temperature", 18) == ESP_OK, "persistent physical-edit setup rejected");
  const auto revision = fixture.state()["configuration"]["revision"].as<uint64_t>();
  auto physical = stockRequest();
  physical[4] = 34;
  physical[5] += 1;
  p::updateCrc(physical);
  fixture.exchange(physical, fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(physical) &&
        fixture.state()["configuration"]["authority"] == true &&
        fixture.state()["configuration"]["revision"].as<uint64_t>() == revision,
        "physical setpoint/service edit revoked persistent operating authority or retired its revision");
  physical[4] = 14;
  physical[13] = p::FixedPowerMode;
  physical[3] = 0;
  p::updateCrc(physical);
  fixture.exchange(physical, fullyStoppedResponse());
  check(fixture.outgoing() == physical &&
        persistedConfig(fixture)["operating"]["mode"] == "temperature",
        "ambient fallback accepted physical mode as the new configuration authority");
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  bool paused = false;
  fake::afterBufferedDataQuery = [&](uart_port_t uart) {
    if (uart != UART_NUM_2 || paused) return;
    paused = true;
    fake::nowUs += 25'000;
  };
  fixture.request();
  fake::afterBufferedDataQuery = {};
  check(paused && fixture.outgoing() == withOperating(stockRequest()) && !fixture.faulted(),
        "former lease/preflight boundary expired persistent operating authority");
  fixture.reply(fullyStoppedResponse());
  fake::uarts[UART_NUM_2].accept = 5;
  fixture.request();
  check(fixture.faulted() && fixture.state()["configuration"]["authority"] == true &&
        fixture.state()["configuration"]["phase"] == "uncertain",
        "partial TX fault erased persistent desired settings or claimed applied status");
  check(persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18,
        "transport uncertainty altered persisted setpoint");
  fake::uarts[UART_NUM_2].accept = -1;
  fixture.rearm();
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(stockRequest()),
        "manual bridge reset failed to reapply persistent desired settings");
  check(fixture.outgoing()[2] == p::NoCommand, "bridge reset replayed a one-shot command");
  std::cout << "configuration: physical-edit priority, final submission stability, fault persistence and reset reapplication passed\n";
}
void testServiceAdoptionAndDeferredPersistence() {
  Fixture fixture;
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  check(fixture.state()["configuration"]["serviceAuthority"] == false && fake::configEvents == 0,
        "header78 request became the initial persistent service authority");
  auto request = qualifyingServiceRequest();
  request[5] = 7;
  p::updateCrc(request);
  fixture.exchange(request, fullyStoppedResponse());
  check(fixture.outgoing() == request &&
        fixture.state()["configuration"]["serviceAuthority"] == false,
        "invalid display service tuple was adopted or mutated before adoption");
  for (unsigned variant = 0; variant < 2; ++variant) {
    request = qualifyingServiceRequest();
    if (variant == 0) request[2] = p::StartCommand;
    else request[17] = 0x5a;
    p::updateCrc(request);
    fixture.exchange(request, fullyStoppedResponse());
    check(fixture.state()["configuration"]["serviceAuthority"] == false,
          "command or priming request seeded persistent authority");
  }

  request = qualifyingServiceRequest();
  const auto priorEvents = fake::configEvents;
  fake::initialized = false;
  fake::inject(UART_NUM_1, request);
  fake::nowUs += 1000;
  fake::inOwnerStep = true;
  fixture.driver->_impl->step();
  fake::inOwnerStep = false;
  check(fixture.state()["configuration"]["serviceAuthority"] == true && fake::configEvents == priorEvents,
        "first valid normal76 request failed adoption or synchronously saved from UART owner");
  fixture.driver->pollSensors();
  check(fake::configEvents == priorEvents, "automatic adoption published before application initialization");
  fake::initialized = true;
  fixture.driver->pollSensors();
  check(fake::configEvents == priorEvents + 1 && !fake::configPublishedInOwner,
        "automatic adoption was not deferred to the poll path outside the owner");
  fixture.reply(fullyStoppedResponse());
  checkService(persistedConfig(fixture)["service"], capturedService(),
               "getConfig did not persist the captured service tuple");
  const auto changedPhysical = withService(request);
  fixture.exchange(changedPhysical, fullyStoppedResponse());
  check(fixture.outgoing() == request && fake::configEvents == priorEvents + 1,
        "later physical display service settings replaced or reseeded authority");
  const auto state = fixture.state();
  checkService(state["configuration"]["service"], capturedService(),
               "effective service state followed stale display settings");
  checkService(state["configuration"]["stockService"], selectedService(),
               "physical display service readout was lost");
  checkService(state["configuration"]["submittedService"], capturedService(),
               "submitted service readout did not describe actual wire bytes");
  std::cout << "service: one-time valid76 adoption, deferred persistence and stock/effective/submitted separation passed\n";
}

void testServicePersistenceRestoreAndStrictJson() {
  {
    Fixture fixture;
    check(restoreService(fixture) && !restoreService(fixture),
          "offline internal restore or unchanged-config detection failed");
    check(fake::configEvents == 0, "restoring persisted data requested another automatic save");
    const auto saved = persistedConfig(fixture);
    check(saved.size() == 1 && saved["service"].is<JsonObjectConst>(),
          "getConfig persisted runtime guards, mode lease or transport state");
    checkService(saved["service"], selectedService(), "persistent service snapshot lost exact values");
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == withService(qualifyingServiceRequest()),
          "first display request overwrote restored service authority");
  }
  {
    Fixture fixture;
    esp32m::Request request;
    populateServiceConfig(request, fixture.state());
    esp32m::RequestContext context(request);
    check(!fixture.driver->setConfig(context) && context.errors.error != ESP_OK,
          "external service update accepted before fresh paired traffic");
  }
  Fixture fixture;
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  const auto original = persistedConfig(fixture);
  const auto revision = fixture.state()["configuration"]["revision"].as<uint64_t>();
  auto reject = [&](auto mutate, bool internal = false) {
    esp32m::Request request;
    populateServiceConfig(request, fixture.state());
    request.internal = internal;
    if (internal) {
      request.payload.remove("expectedBootId");
      request.payload.remove("expectedRevision");
      request.payload.remove("acceptBeforeUptimeMs");
    }
    mutate(request);
    esp32m::RequestContext context(request);
    check(!fixture.driver->setConfig(context) && context.errors.error != ESP_OK,
          "invalid service config accepted or partially changed");
    const auto current = persistedConfig(fixture);
    checkService(current["service"], capturedService(), "invalid config partially altered authority");
    check(fixture.state()["configuration"]["revision"].as<uint64_t>() == revision,
          "rejected service config retired valid guards");
  };
  reject([](auto& r) { r.payload["service"] = nullptr; });
  reject([](auto& r) { r.payload["service"].template to<JsonObject>(); });
  reject([](auto& r) { r.payload["service"].remove("minimumPumpHz"); });
  reject([](auto& r) { r.payload["service"]["unknown"] = 1; });
  reject([](auto& r) { r.payload["unknown"] = 1; });
  reject([](auto& r) { r.payload["service"]["minimumPumpHz"] = 0.7; });
  reject([](auto& r) { r.payload["service"]["minimumPumpHz"] = 2.6; });
  reject([](auto& r) { r.payload["service"]["minimumPumpHz"] = 1.55; });
  reject([](auto& r) { r.payload["service"]["minimumPumpHz"] = "1.5"; });
  reject([](auto& r) { r.payload["service"]["minimumPumpHz"] = true; });
  reject([](auto& r) { r.payload["service"]["minimumPumpHz"] = nullptr; });
  reject([](auto& r) { r.payload["service"]["minimumPumpHz"] = 2.5; r.payload["service"]["maximumPumpHz"] = 2.2; });
  reject([](auto& r) { r.payload["service"]["maximumPumpHz"] = 2.1; });
  reject([](auto& r) { r.payload["service"]["maximumPumpHz"] = 8.1; });
  reject([](auto& r) { r.payload["service"]["minimumFanRpm"] = 1351; });
  reject([](auto& r) { r.payload["service"]["minimumFanRpm"] = 1600.5; });
  reject([](auto& r) { r.payload["service"]["maximumFanRpm"] = 5001; });
  reject([](auto& r) { r.payload["service"]["configuredVoltageV"] = 12.1; });
  reject([](auto& r) { r.payload["service"]["hallCount"] = 0; });
  reject([](auto& r) { r.payload["service"]["hallCount"] = 1.5; });
  reject([](auto& r) { r.payload["service"]["glowPowerLevel"] = 7; });
  reject([](auto& r) { r.payload["service"]["glowPowerLevel"] = true; });
  reject([](auto& r) { r.broadcast = true; });
  reject([](auto& r) { r.payload["service"]["glowPowerLevel"] = 7; }, true);
  reject([](auto& r) { r.payload["service"] = nullptr; }, true);
  (void)original;

  esp32m::Request accepted;
  populateServiceConfig(accepted, fixture.state());
  esp32m::RequestContext acceptedContext(accepted);
  check(fixture.driver->setConfig(acceptedContext) && acceptedContext.errors.error == ESP_OK,
        "strict valid guarded service config was rejected");
  checkService(persistedConfig(fixture)["service"], selectedService(),
               "accepted service config not returned by getConfig");
  check(fixture.state()["configuration"]["revision"].as<uint64_t>() != revision,
        "service save did not retire prior external drafts");
  esp32m::RequestContext replayContext(accepted);
  check(!fixture.driver->setConfig(replayContext) && replayContext.errors.error == ESP_OK,
        "unchanged service save required a fresh UI snapshot or changed authority");
  std::cout << "service: offline persistence restore, exact config snapshot and strict atomic explicit JSON passed\n";
}

void testServiceEligibilityAndOverlay() {
  {
    Fixture fixture;
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    fixture.request(qualifyingServiceRequest());
    esp32m::Request request;
    populateServiceConfig(request, fixture.state());
    esp32m::RequestContext context(request);
    check(fixture.state()["configuration"]["serviceCanApply"] == true &&
          fixture.driver->setConfig(context) && context.errors.error == ESP_OK &&
          fixture.outgoing() == qualifyingServiceRequest(),
          "transient pairing flickered service availability, rejected a pending save or mutated in-flight bytes");
    fixture.reply(fullyStoppedResponse());
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == withService(qualifyingServiceRequest()),
          "mid-transaction service save missed the next physical request");
    const auto service = selectedService();
    for (unsigned variant = 0; variant < 4; ++variant) {
      auto requestFrame = qualifyingServiceRequest();
      if (variant == 0) requestFrame[0] = 0x78;
      if (variant == 1) requestFrame[2] = p::StartCommand;
      if (variant == 2) requestFrame[2] = p::StopCommand;
      if (variant == 3) requestFrame[17] = 0x5a;
      p::updateCrc(requestFrame);
      fixture.exchange(requestFrame, fullyStoppedResponse());
      check(fixture.outgoing() == withService(requestFrame, service),
            "persistent overlay lost command/prime/opaque bytes or skipped a normal header");
    }
  }
  for (unsigned variant = 0; variant < 5; ++variant) {
    Fixture fixture;
    auto response = fullyStoppedResponse();
    if (variant == 0) response = heaterResponse(5, 1);
    if (variant == 1) response = heaterResponse(0, 0);
    if (variant == 2) response = fullyStoppedResponse(), response[3] = 10;
    p::updateCrc(response);
    fixture.exchange(qualifyingServiceRequest(), response);
    if (variant == 3) fake::nowUs += 2'000'001;
    if (variant == 4) check(fixture.driver->turn(true) == ESP_OK, "queued command service guard setup");
    esp32m::Request request;
    populateServiceConfig(request, fixture.state());
    esp32m::RequestContext context(request);
    check(!fixture.driver->setConfig(context) && context.errors.error != ESP_OK,
          "service save accepted running/fan/pump/error/stale/queued-command state");
    checkService(persistedConfig(fixture)["service"], capturedService(),
                 "ineligible save changed persistent authority");
  }
  {
    Fixture fixture(true);
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    check(configure(fixture, "temperature", 18) == ESP_OK, "persistent mode/service setup");
    check(saveService(fixture), "service save was blocked by permanent operating authority");
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == withOperating(withService(qualifyingServiceRequest())) &&
          persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18,
          "service save replaced persistent mode/value authority");
  }
  std::cout << "service: stable fresh-traffic eligibility, research/command exclusion and exact command/prime overlay passed\n";
}

void testServiceSurvivesOperatingResetAndFinalPreflight() {
  Fixture fixture(true);
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(saveService(fixture), "service lifecycle setup save rejected");
  fixture.driver->_impl->_options.requireFixedPowerTemplate = true;
  fixture.driver->_impl->_options.fixedPowerTemplate = qualifyingServiceRequest();
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(fixture.state()["configuration"]["powerAvailable"] == false,
        "raw-power qualification compared stale display instead of effective service profile");
  fixture.driver->_impl->_options.fixedPowerTemplate = withService(qualifyingServiceRequest());
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(configure(fixture, "power", 13) == ESP_OK,
        "effective service profile failed qualified raw-power comparison");
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(fixture.outgoing()[13] == p::FixedPowerMode && fixture.outgoing()[4] == 13,
        "service overlay displaced active mode/value lease");
  fixture.tick(11'000);
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(withService(qualifyingServiceRequest()), "power", 13),
        "persistent operating application lost service authority");
  check(fixture.driver->releaseOverride() == ESP_OK, "service lifecycle release rejected");
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(withService(qualifyingServiceRequest()), "power", 13),
        "releaseOverride cleared persistent operating/service authority");
  check(configure(fixture, "temperature", 18) == ESP_OK,
        "persistent service physical-takeover setup rejected");
  auto changedPhysical = qualifyingServiceRequest();
  changedPhysical[5] = 18;
  p::updateCrc(changedPhysical);
  fixture.exchange(changedPhysical, fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(withService(changedPhysical)) &&
        fixture.state()["configuration"]["active"] == true &&
        fixture.state()["configuration"]["serviceCanApply"] == true,
        "physical service edit replaced persistent operating/service authority");
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  fake::uarts[UART_NUM_2].accept = 5;
  fixture.request(qualifyingServiceRequest());
  check(fixture.faulted(), "service lifecycle partial TX failed to fault");
  checkService(persistedConfig(fixture)["service"], selectedService(),
               "transport fault cleared persistent service authority");
  fake::uarts[UART_NUM_2].accept = -1;
  fixture.rearm();
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(withService(qualifyingServiceRequest())),
        "bridge reset reseeded operating/service values from the display");

  // Change the persisted tuple after preflight began but before FIFO submission.
  // This models a restore racing frame preparation without inventing a second
  // transport implementation; final submission must reapply current authority.
  auto lastMoment = selectedService();
  lastMoment.minimumPumpDecihz = 17;
  bool restored = false;
  fake::afterBufferedDataQuery = [&](uart_port_t uart) {
    if (uart == UART_NUM_2 && !restored) {
      restored = true;
      restoreService(fixture, lastMoment);
      restoreOperating(fixture, "temperature", 19);
    }
  };
  fixture.request(qualifyingServiceRequest());
  fake::afterBufferedDataQuery = {};
  check(restored && fixture.outgoing() == withOperating(withService(qualifyingServiceRequest(), lastMoment),
                                                       "temperature", 19),
        "final UART submission used stale prepared service or operating values");
  fixture.reply(fullyStoppedResponse());
  check(!fake::configPublishedInOwner, "service lifecycle published configuration from UART owner");
  std::cout << "service: effective power qualification, permanent operating coexistence, fault/reset and final-UART overlay passed\n";
}

void testServiceBootAndBypassPersistence() {
  {
    fake::reset();
    fake::initialized = false;
    Driver::Options options({GPIO_NUM_13, GPIO_NUM_14, UART_NUM_2},
                            {GPIO_NUM_11, GPIO_NUM_12, UART_NUM_1});
    Driver driver("restore-before-arm", options);
    fake::nowUs += 21'000;
    driver._impl->step();
    check(!driver._impl->_active && fake::pinModes[GPIO_NUM_12] == GPIO_MODE_INPUT &&
          fake::pinModes[GPIO_NUM_14] == GPIO_MODE_INPUT,
          "bridge armed before application initialization restored persistent config");
    esp32m::Request request;
    request.internal = true;
    populateService(request.payload["service"].to<JsonObject>(), selectedService());
    populateOperating(request.payload["operating"].to<JsonObject>());
    esp32m::RequestContext context(request);
    check(driver.setConfig(context) && context.errors.error == ESP_OK,
          "pre-initialization internal service restore rejected");
    driver.pollSensors();
    check(fake::configEvents == 0, "pre-initialization restore published a config event");
    fake::initialized = true;
    driver._impl->step();
    check(driver._impl->_active, "bridge failed to arm after persistent load completed");
    fake::inject(UART_NUM_1, qualifyingServiceRequest());
    fake::nowUs += 1000;
    driver._impl->step();
    const auto& bytes = fake::uarts[UART_NUM_2].submitted.back();
    const auto expected = withOperating(withService(qualifyingServiceRequest()));
    check(bytes == std::vector<uint8_t>(expected.begin(), expected.end()),
          "first post-boot request escaped persistent service substitution");
  }
  {
    Fixture fixture(false, true);
    check(restoreService(fixture), "bypass restore rejected");
    check(restoreOperating(fixture), "bypass operating restore rejected");
    fixture.enterIntercept();
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == withOperating(withService(qualifyingServiceRequest())),
          "initial bypass qualification replaced persisted service authority");
    check(fixture.driver->setBypass(true) == ESP_OK, "persistent service requested bypass rejected");
    checkService(persistedConfig(fixture)["service"], selectedService(),
                 "requested bypass cleared persistent service authority");
    fixture.qualify();
    check(fixture.driver->setBypass(false) == ESP_OK, "persistent service bypass release rejected");
    fixture.tick();
    fixture.enterIntercept();
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == withOperating(withService(qualifyingServiceRequest())) && fake::configEvents == 0,
          "bypass requalification reseeded or discarded persisted service authority");
  }
  std::cout << "service: restore-before-arm initialization and requested-bypass persistence passed\n";
}

void testOperatingPersistenceAndStrictConfig() {
  JsonDocument saved;
  {
    Fixture fixture(true);
    check(persistedConfig(fixture).size() == 0, "unobserved operating defaults were invented");
    check(restoreOperating(fixture, "power", 14), "offline operating restore failed");
    esp32m::Request request;
    request.internal = true;
    populateOperating(request.payload["operating"].to<JsonObject>(), "power", 14);
    request.payload["operating"]["temperatureSetpointC"] = 19;
    esp32m::RequestContext context(request);
    check(fixture.driver->setConfig(context) && context.errors.error == ESP_OK,
          "offline restore of both remembered setpoints failed");
    saved = persistedConfig(fixture);
    check(saved.size() == 1 && saved["operating"].size() == 3 &&
          saved["operating"]["mode"] == "power" &&
          saved["operating"]["temperatureSetpointC"] == 19 &&
          saved["operating"]["fixedPowerSettingRaw"] == 14 && fake::configEvents == 0,
          "offline operating config included runtime fields, lost values or requested a redundant save");
  }
  Fixture fixture(true);
  esp32m::Request restored;
  restored.internal = true;
  restored.payload.set(saved);
  esp32m::RequestContext restoreContext(restored);
  check(fixture.driver->setConfig(restoreContext) && restoreContext.errors.error == ESP_OK,
        "serialized operating config failed offline reboot restore");
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(fixture.outgoing() == withOperating(qualifyingServiceRequest(), "power", 14) &&
        persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 19,
        "initial physical request reseeded restored mode or a remembered setpoint");
  const auto revision = fixture.state()["configuration"]["revision"].as<uint64_t>();
  auto reject = [&](auto mutate, bool internal = false) {
    esp32m::Request request;
    populateOperatingConfig(request, fixture.state(), "temperature", 18);
    request.internal = internal;
    if (internal) {
      request.payload.remove("expectedBootId");
      request.payload.remove("expectedRevision");
      request.payload.remove("acceptBeforeUptimeMs");
    }
    populateService(request.payload["service"].to<JsonObject>(), selectedService());
    mutate(request.payload);
    esp32m::RequestContext context(request);
    check(!fixture.driver->setConfig(context) && context.errors.error != ESP_OK,
          "invalid operating config was accepted");
    const auto current = persistedConfig(fixture);
    check(current["operating"]["mode"] == "power" &&
          current["operating"]["temperatureSetpointC"] == 19 &&
          current["operating"]["fixedPowerSettingRaw"] == 14 &&
          fixture.state()["configuration"]["revision"].as<uint64_t>() == revision,
          "invalid operating config partially committed a valid service or operating half");
    checkService(current["service"], capturedService(), "invalid operating half changed service authority");
  };
  reject([](auto& d) { d["operating"] = nullptr; });
  reject([](auto& d) { d["operating"].template to<JsonObject>(); });
  reject([](auto& d) { d["operating"].remove("mode"); });
  reject([](auto& d) { d["operating"].remove("temperatureSetpointC"); });
  reject([](auto& d) { d["operating"]["mode"] = "unknown"; });
  reject([](auto& d) { d["operating"]["mode"] = std::string("temperature\0power", 17); });
  reject([](auto& d) { d["operating"]["temperatureSetpointC"] = nullptr; });
  reject([](auto& d) { d["operating"]["temperatureSetpointC"] = 18.5; });
  reject([](auto& d) { d["operating"]["temperatureSetpointC"] = true; });
  reject([](auto& d) { d["operating"]["temperatureSetpointC"] = 7; });
  reject([](auto& d) { d["operating"]["temperatureSetpointC"] = 36; });
  reject([](auto& d) { d["operating"]["fixedPowerSettingRaw"] = 15; });
  reject([](auto& d) { d["operating"]["fixedPowerSettingRaw"] = 13.5; });
  reject([](auto& d) { d["operating"]["leaseMs"] = 60000; });
  reject([](auto& d) { d["operating"]["temperatureSetpointC"] = 128; }, true);
  reject([](auto& d) { d["operating"]["mode"] = "power"; d["operating"]["fixedPowerSettingRaw"] = 256; }, true);
  reject([](auto& d) { d["operating"]["temperatureSetpointC"] = nullptr; }, true);
  esp32m::Request empty;
  empty.internal = true;
  empty.payload.to<JsonObject>();
  esp32m::RequestContext emptyContext(empty);
  check(!fixture.driver->setConfig(emptyContext) && emptyContext.errors.error == ESP_OK &&
        persistedConfig(fixture)["operating"]["mode"] == "power",
        "empty legacy restore erased persistent operating authority");
  std::cout << "configuration: offline reboot roundtrip, unknown-setpoint preservation and strict atomic persisted config passed\n";
}

void testDeferredConfigurationResponseAndOwnedRequest() {
  Fixture fixture(true);
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  const auto events = fake::configEvents;
  std::shared_ptr<fake::DeferredResponse> response;
  {
    esp32m::Request request;
    request.requestedName = "config-set";
    populateOperatingConfig(request, fixture.state());
    response = request.deferred;
    check(fixture.driver->handleRequest(request) && fake::pendingWorker &&
          response->published == 0 && fake::configEvents == events &&
          persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 35,
          "config-set blocked the caller, committed inline or published an early response");
    request.payload["operating"]["temperatureSetpointC"] = 99;
  }
  unsigned stage = 0;
  fake::afterTaskDelay = [&] {
    check(response->published == 0, "deferred response preceded a new complete forwarded exchange");
    check(persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18 &&
          fake::configEvents == events + 1 && fake::broadcastEvents == 1,
          "worker lost copied request data or did not persist exactly once");
    if (stage == 0) {
      fixture.request(qualifyingServiceRequest());
      check(fixture.outgoing() == withOperating(qualifyingServiceRequest()),
            "deferred worker's settings were not applied to the next request");
    } else if (stage == 1) {
      fake::inject(UART_NUM_2, fullyStoppedResponse());
      fixture.tick();
      check(fixture.driver->_impl->_sides[0].echoPending != 0,
            "deferred-response test skipped the outstanding display echo boundary");
    } else fixture.tick();
    ++stage;
  };
  fake::runPendingWorker();
  fake::afterTaskDelay = {};
  check(stage == 3 && response->published == 1 && !response->error &&
        response->data["operating"]["temperatureSetpointC"] == 18 &&
        !fixture.state()["configuration"]["updatePending"].as<bool>() &&
        !fake::configPublishedInOwner,
        "config-set did not finish once after request/reply/both local echoes outside the UART owner");
  std::cout << "configuration async: owned request lifetime, deferred response and complete new wire exchange passed\n";
}

void testDeferredConfigurationNoopAndBoundedWorker() {
  Fixture fixture(true);
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  esp32m::Request request;
  request.requestedName = "config-set";
  populateOperatingConfig(request, fixture.state(), "temperature", 35);
  request.payload["expectedBootId"] = "old-visible-boot";
  request.payload["expectedRevision"] = 0;
  request.payload["acceptBeforeUptimeMs"] = 1;
  const auto events = fake::configEvents;
  check(fixture.driver->handleRequest(request) && request.deferred->published == 0,
        "unchanged config-set responded immediately from an old completed exchange");
  esp32m::Request busy;
  busy.requestedName = "config-set";
  populateOperatingConfig(busy, fixture.state(), "temperature", 18);
  check(fixture.driver->handleRequest(busy) && busy.responseError == ESP_ERR_INVALID_STATE &&
        fake::createdWorkers == 1, "concurrent config-set created an unbounded second worker");
  unsigned stage = 0;
  fake::afterTaskDelay = [&] {
    check(request.deferred->published == 0, "no-op save reused a pre-admission reply");
    if (stage++ == 0) fixture.request(qualifyingServiceRequest());
    else fixture.reply(fullyStoppedResponse());
  };
  fake::runPendingWorker();
  fake::afterTaskDelay = {};
  check(stage == 2 && request.deferred->published == 1 && !request.deferred->error &&
        fake::configEvents == events && fake::broadcastEvents == 0,
        "no-op save required UI reload, failed a new exchange or duplicated persistence");
  esp32m::Request broadcast;
  broadcast.requestedName = "config-set";
  broadcast.broadcast = true;
  populateOperatingConfig(broadcast, fixture.state());
  check(fixture.driver->handleRequest(broadcast) && broadcast.responseError == ESP_ERR_INVALID_ARG &&
        fake::createdWorkers == 1, "broadcast config-set acquired the worker");
  esp32m::Request internal;
  internal.requestedName = "config-set";
  internal.internal = true;
  check(!fixture.driver->handleRequest(internal) && fake::createdWorkers == 1,
        "internal restore was intercepted by the asynchronous external route");
  std::cout << "configuration async: bounded worker, no-op new exchange and stale visible metadata acceptance passed\n";
}

void testDeferredConfigurationFailureRetention() {
  for (unsigned variant = 0; variant < 5; ++variant) {
    Fixture fixture(true);
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    esp32m::Request request;
    request.requestedName = "config-set";
    populateOperatingConfig(request, fixture.state());
    if (variant == 0) request.payload["operating"]["temperatureSetpointC"] = 18.5;
    if (variant == 1) fake::failCreateWorker = true;
    if (variant == 2) request.failResponseCreation = true;
    const auto submissions = fake::uarts[UART_NUM_2].submitted.size();
    check(fixture.driver->handleRequest(request), "external config-set route not handled");
    if (variant <= 2) {
      fake::runPendingWorker();
      check((request.deferred->published == 1 && request.deferred->error) ||
            request.responseError == ESP_ERR_NO_MEM,
            "invalid request or worker/response allocation failure reported success");
      check(persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 35 &&
            fake::uarts[UART_NUM_2].submitted.size() == submissions,
            "failed admission partially committed or transmitted settings");
    } else {
      bool changed = false;
      fake::afterTaskDelay = [&] {
        if (changed) return;
        changed = true;
        if (variant == 3) {
          check(fixture.driver->setTemperature(19) == ESP_OK, "superseded-worker setup rejected");
        } else {
          fake::uarts[UART_NUM_2].accept = 5;
          fixture.request(qualifyingServiceRequest());
        }
      };
      fake::runPendingWorker();
      fake::afterTaskDelay = {};
      check(request.deferred->published == 1 && request.deferred->error &&
            persistedConfig(fixture)["operating"]["temperatureSetpointC"] == (variant == 3 ? 19 : 18),
            "superseded/faulted worker retried, rolled back or reported false success");
      if (variant == 4)
        check(fixture.faulted() && fake::uarts[UART_NUM_2].submitted.size() == submissions + 1,
              "uncertain partial TX was retried by the configuration worker");
    }
    check(!fixture.state()["configuration"]["updatePending"].as<bool>() &&
          !fake::configPublishedInOwner, "failed configuration worker leaked ownership or published inside UART handling");
  }
  {
    Fixture fixture(true);
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    fixture.driver->_impl->_options.freshnessMs = 10000;
    esp32m::Request request;
    request.requestedName = "config-set";
    populateOperatingConfig(request, fixture.state());
    fixture.driver->handleRequest(request);
    const auto submissions = fake::uarts[UART_NUM_2].submitted.size();
    fake::afterTaskDelay = [&] { fixture.tick(); };
    fake::runPendingWorker();
    fake::afterTaskDelay = {};
    check(request.deferred->published == 1 && request.deferred->error &&
          !fixture.faulted() && persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18 &&
          fake::uarts[UART_NUM_2].submitted.size() == submissions,
          "missing next exchange did not time out once while retaining settings without retry");
  }
  std::cout << "configuration async: invalid/allocation/superseded/fault/timeout results retain saved settings without retry passed\n";
}

void testDeferredServiceSaveDuringAmbientFallback() {
  Fixture fixture(true);
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(configure(fixture, "temperature", 18) == ESP_OK, "service fallback setup rejected");
  auto physicalPower = qualifyingServiceRequest();
  physicalPower[3] = 0;
  physicalPower[4] = 14;
  physicalPower[13] = p::FixedPowerMode;
  p::updateCrc(physicalPower);
  fixture.exchange(physicalPower, fullyStoppedResponse());
  check(fixture.state()["configuration"]["phase"] == "suspended" &&
        fixture.state()["configuration"]["serviceCanApply"] == true,
        "ambient fallback incorrectly prevented a separate stopped-heater service edit");
  esp32m::Request request;
  request.requestedName = "config-set";
  populateServiceConfig(request, fixture.state());
  check(fixture.driver->handleRequest(request), "service-only deferred route not handled");
  unsigned stage = 0;
  fake::afterTaskDelay = [&] {
    check(request.deferred->published == 0, "service fallback save responded before its new pair");
    if (stage++ == 0) fixture.request(physicalPower);
    else fixture.reply(fullyStoppedResponse());
  };
  fake::runPendingWorker();
  fake::afterTaskDelay = {};
  check(stage == 2 && request.deferred->published == 1 && !request.deferred->error &&
        fixture.outgoing() == withService(physicalPower) &&
        fixture.state()["configuration"]["active"] == false &&
        persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18,
        "service-only save required an unavailable thermostat overlay or changed its saved desired mode");
  checkService(request.deferred->data["service"], selectedService(),
               "successful service fallback response omitted the applied service tuple");
  esp32m::Request combined;
  combined.requestedName = "config-set";
  populateOperatingConfig(combined, fixture.state(), "temperature", 19);
  auto invalidService = selectedService();
  invalidService.minimumPumpDecihz = 17;
  populateService(combined.payload["service"].to<JsonObject>(), invalidService);
  fixture.driver->handleRequest(combined);
  fake::runPendingWorker();
  check(combined.deferred->published == 1 && combined.deferred->error &&
        persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18,
        "combined edit succeeded without required thermostat ambient or partially changed the desired value");
  checkService(persistedConfig(fixture)["service"], selectedService(),
               "rejected combined edit partially changed service authority");
  std::cout << "configuration async: service-only save confirms during retained thermostat ambient fallback passed\n";
}

void testPersistentPowerRuntimeQualification() {
  for (const int savedRaw : {0, 255}) {
    Fixture fixture(true);
    fixture.driver->_impl->_options.requireFixedPowerTemplate = true;
    fixture.driver->_impl->_options.fixedPowerTemplate = qualifyingServiceRequest();
    check(restoreOperating(fixture, "power", savedRaw), "offline raw-power restore rejected");
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == qualifyingServiceRequest() &&
          fixture.state()["configuration"]["authority"] == true &&
          fixture.state()["configuration"]["active"] == false &&
          fixture.state()["configuration"]["phase"] == "suspended" &&
          persistedConfig(fixture)["operating"]["fixedPowerSettingRaw"] == savedRaw,
          "offline restored power escaped caller bounds or was silently erased");
    check(restoreOperating(fixture, "power", 13), "qualified raw-power restore rejected");
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == withOperating(qualifyingServiceRequest(), "power", 13) &&
          fixture.state()["configuration"]["active"] == true,
          "qualified replacement failed to resume persistent raw power");
  }
  {
    Fixture fixture;
    check(restoreOperating(fixture, "power", 13), "unqualified installation restore rejected");
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    check(fixture.outgoing() == qualifyingServiceRequest() &&
          fixture.state()["configuration"]["phase"] == "suspended" &&
          persistedConfig(fixture)["operating"]["fixedPowerSettingRaw"] == 13,
          "disabled caller power range became enabled through persisted restore");
  }
  {
    Fixture fixture(true);
    fixture.driver->_impl->_options.requireFixedPowerTemplate = true;
    fixture.driver->_impl->_options.fixedPowerTemplate = stockRequest();
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    check(configure(fixture, "power", 13) == ESP_OK, "physical-profile runtime setup rejected");
    fixture.exchange(stockRequest(), fullyStoppedResponse());
    for (unsigned variant = 0; variant < 2; ++variant) {
      auto changed = stockRequest();
      if (variant == 0) ++changed[18];
      else changed[0] = 0x76;
      p::updateCrc(changed);
      fixture.exchange(changed, fullyStoppedResponse());
      check(fixture.outgoing() == changed &&
            fixture.state()["configuration"]["phase"] == "suspended" &&
            persistedConfig(fixture)["operating"]["fixedPowerSettingRaw"] == 13,
            "opaque/header profile change retained unqualified power output or erased desired power");
      fixture.exchange(stockRequest(), fullyStoppedResponse());
      check(fixture.outgoing() == withOperating(stockRequest(), "power", 13),
            "restored qualified physical profile did not automatically resume power authority");
    }
  }
  std::cout << "configuration: restored raw bounds, disabled power and opaque/header runtime qualification with resume passed\n";
}

void testDeferredServiceSaveSuspendsAndResumesPower() {
  Fixture fixture(true);
  fixture.driver->_impl->_options.requireFixedPowerTemplate = true;
  fixture.driver->_impl->_options.fixedPowerTemplate = qualifyingServiceRequest();
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  check(configure(fixture, "power", 13) == ESP_OK, "service-profile power setup rejected");
  fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
  auto save = [&](const p::ServiceParameters& service, bool powerApplied) {
    esp32m::Request request;
    request.requestedName = "config-set";
    populateServiceConfig(request, fixture.state(), service);
    check(fixture.driver->handleRequest(request), "service profile save route rejected");
    unsigned stage = 0;
    fake::afterTaskDelay = [&] {
      check(request.deferred->published == 0, "service profile save responded before forwarding");
      if (stage++ == 0) fixture.request(qualifyingServiceRequest());
      else fixture.reply(fullyStoppedResponse());
    };
    fake::runPendingWorker();
    fake::afterTaskDelay = {};
    const auto serviceFrame = withService(qualifyingServiceRequest(), service);
    const auto expected = powerApplied ? withOperating(serviceFrame, "power", 13) : serviceFrame;
    check(stage == 2 && request.deferred->published == 1 && !request.deferred->error &&
          fixture.outgoing() == expected &&
          fixture.state()["configuration"]["active"].as<bool>() == powerApplied &&
          persistedConfig(fixture)["operating"]["mode"] == "power" &&
          persistedConfig(fixture)["operating"]["fixedPowerSettingRaw"] == 13,
          "service-only confirmation required unqualified power output, erased desired power or failed qualified resume");
    checkService(request.deferred->data["service"], service,
                 "service profile response did not report the confirmed authoritative tuple");
  };
  save(selectedService(), false);
  check(fixture.state()["configuration"]["phase"] == "suspended" &&
        fixture.state()["configuration"]["powerAvailable"] == false,
        "changed service profile did not suspend caller-qualified power control");
  save(capturedService(), true);
  check(fixture.state()["configuration"]["powerAvailable"] == true,
        "restored service profile failed power qualification");
  std::cout << "configuration async: service-only profile edits confirm while suspending and automatically resuming saved power passed\n";
}

void testDeferredConfigurationInterruptedThenRecovered() {
  for (unsigned variant = 0; variant < 2; ++variant) {
    Fixture fixture(true, variant == 1);
    if (variant == 1) fixture.enterIntercept();
    fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
    esp32m::Request request;
    request.requestedName = "config-set";
    populateOperatingConfig(request, fixture.state());
    check(fixture.driver->handleRequest(request), "interrupted configuration job was not handled");
    unsigned delays = 0;
    fake::afterTaskDelay = [&] {
      if (delays++ != 0) return;
      if (variant == 0) {
        fake::uarts[UART_NUM_2].accept = 5;
        fixture.request(qualifyingServiceRequest());
        check(fixture.faulted(), "between-polls partial TX did not interrupt transport");
        fake::uarts[UART_NUM_2].accept = -1;
        fixture.rearm();
      } else {
        check(fixture.driver->setBypass(true) == ESP_OK, "between-polls bypass entry rejected");
        check(fixture.driver->setBypass(false) == ESP_OK, "between-polls bypass release rejected");
        fixture.tick();
        fixture.enterIntercept();
      }
      fixture.exchange(qualifyingServiceRequest(), fullyStoppedResponse());
      check(!fixture.faulted() && fixture.driver->_impl->_active &&
            fixture.outgoing() == withOperating(qualifyingServiceRequest()) &&
            fixture.driver->_impl->_paired,
            "between-polls transport recovery did not produce a fresh matching exchange");
    };
    fake::runPendingWorker();
    fake::afterTaskDelay = {};
    check(delays == 1 && request.deferred->published == 1 && request.deferred->error &&
          !fixture.faulted() && fixture.driver->_impl->_active &&
          persistedConfig(fixture)["operating"]["temperatureSetpointC"] == 18 &&
          !fixture.state()["configuration"]["updatePending"].as<bool>(),
          "configuration worker missed an interruption/recovery between polls or erased retained settings");
  }
  std::cout << "configuration async: fault/reset and bypass/requalification history cannot become false successful confirmations passed\n";
}

void testThermostatTransientControl() {
  using namespace esp32m::dev::thermostat;
  Fixture fixture;
  Actuator& actuator = *fixture.driver;
  fixture.exchange(stockRequest(), fullyStoppedResponse());
  const auto baseline = fixture.outgoing()[4];
  auto feedback = actuator.snapshot();
  const auto ambientSample = fixture.driver->_impl->_ambient.sampledAtMs;
  const auto bodySample = fixture.driver->_impl->_body.sampledAtMs;
  check(ambientSample == fixture.driver->_impl->_displayStamp / 1000 &&
        bodySample == fixture.driver->_impl->_heaterStamp / 1000,
        "runtime sensors must preserve the acquisition timestamps of their physical packets");
  fake::nowUs += 2000;
  fixture.driver->_impl->_sensorDirty = true;
  fixture.driver->pollSensors();
  check(fixture.driver->_impl->_ambient.sampledAtMs == ambientSample &&
        fixture.driver->_impl->_body.sampledAtMs == bodySample,
        "cached sensor publication must not renew room-temperature acquisition time");
  check(feedback.valid && feedback.phase == Phase::Off && feedback.nativeTemperature.valid,
        "thermostat snapshot must use fresh correlated heater/display samples");
  check(!feedback.minimumKnown, "native low pump threshold must require installation qualification");
  check(actuator.capabilities().nativeTarget &&
        !actuator.capabilities().externalAmbient,
        "native target and unqualified ambient substitution capabilities differ");
  Request target;
  target.command = Command::NativeTarget;
  target.expectedRevision = feedback.revision;
  target.validUntilMs = fake::nowUs / 1000 + 50;
  target.targetC = baseline - 1;
  const auto persisted = fake::configEvents;
  check(actuator.apply(target) == Result::Accepted, "native transient target rejected");
  fixture.exchange();
  check(fixture.outgoing()[4] == baseline - 1 && fake::configEvents == persisted,
        "supervisor regulation changed persistent settings or failed to overlay target");
  while (fake::nowUs / 1000 <= target.validUntilMs) fixture.exchange();
  fixture.exchange();
  check(fixture.outgoing()[4] == baseline, "expired transient lease did not restore native settings");
  target.validUntilMs = fake::nowUs / 1000 + 1000;
  check(actuator.apply(target) == Result::Accepted, "fresh lease renewal rejected");
  auto local = stockRequest();
  ++local[4];
  p::updateCrc(local);
  fixture.exchange(local, fullyStoppedResponse());
  check(actuator.snapshot().revision != target.expectedRevision &&
        actuator.apply(target) == Result::Rejected,
        "physical edit must revoke supervisor authority atomically");
  check(!fixture.driver->_impl->_thermostatOverride, "physical edit left transient overlay active");
  target.expectedRevision = actuator.snapshot().revision;
  target.ambientC = 23;
  check(actuator.apply(target) == Result::Unsupported,
        "unqualified remote ambient injection must not be accepted");
  Request start;
  start.command = Command::Start;
  start.expectedRevision = target.expectedRevision;
  start.validUntilMs = fake::nowUs / 1000 + 5;
  check(actuator.apply(start) == Result::Accepted, "fresh supervisor Start rejected");
  fake::nowUs += 6000;
  fixture.exchange(local);
  check(fixture.outgoing()[2] == p::NoCommand,
        "queued Start escaped after supervisor lease expiry");
  check(fixture.driver->_impl->_lastCommandResult == "cancelled-before-transmission",
        "expired unsent Start must retain cancellation evidence");
  start.expectedRevision = actuator.snapshot().revision;
  start.validUntilMs = fake::nowUs / 1000 + 5000;
  check(actuator.snapshot().phase == Phase::Cooling &&
        actuator.apply(start) == Result::Busy,
        "zero run state with residual fan/pump must not permit a restart");
  fixture.driver->_impl->_options.thermostatLowPumpDecihz = 23;
  fixture.exchange(local, heaterResponse(5, 1));
  check(actuator.snapshot().minimumKnown &&
        actuator.snapshot().atMinimum,
        "qualified running pump floor was not reflected in typed feedback");
  std::cout << "thermostat: typed freshness, transient overlay, lease expiry, physical takeover and no replay passed\n";
}

int main() {
  try {
    testAcquisitionTimes();
    testThermostatTransientControl();
    testProtocol();
    testForwardingAndOneShot();
    testLocalArbitration();
    testFixedPowerBounds();
    testFaultsAndNoReplay();
    testPreflightAndPublication();
    testOptionalBypass();
    testConfigurationAndActionValidation();
    testSchedulingAndFirstFaultEvidence();
    testBufferedEchoAndCorrelationBudget();
    testModeConfigurationAtomicityAndState();
    testModeConfigurationEligibilityAndAmbient();
    testModeConfigurationGuardsAndJson();
    testPersistentConfigurationPhysicalEditsAndFault();
    testServiceAdoptionAndDeferredPersistence();
    testServicePersistenceRestoreAndStrictJson();
    testServiceEligibilityAndOverlay();
    testServiceSurvivesOperatingResetAndFinalPreflight();
    testServiceBootAndBypassPersistence();
    testOperatingPersistenceAndStrictConfig();
    testDeferredConfigurationResponseAndOwnedRequest();
    testDeferredConfigurationNoopAndBoundedWorker();
    testDeferredConfigurationFailureRetention();
    testDeferredServiceSaveDuringAmbientFallback();
    testPersistentPowerRuntimeQualification();
    testDeferredServiceSaveSuspendsAndResumesPower();
    testDeferredConfigurationInterruptedThenRecovered();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
