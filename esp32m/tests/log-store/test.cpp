#include <ArduinoJson.h>
#include "esp32m/log/storage.hpp"

#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsys = std::filesystem;
namespace esp32m { class AppObject; }
using esp_err_t = int;
using nvs_handle_t = int;
using TaskHandle_t = void*;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 257,
    ESP_ERR_INVALID_ARG = 258, ESP_ERR_INVALID_STATE = 259,
    ESP_ERR_NVS_NOT_FOUND = 260, NVS_READWRITE = 0;
constexpr int HTTP_GET = 0, HTTPD_400_BAD_REQUEST = 400,
    HTTPD_500_INTERNAL_SERVER_ERROR = 500;
constexpr int pdTRUE = 1, pdPASS = 1, taskSCHEDULER_SUSPENDED = 0,
    RINGBUF_TYPE_NOSPLIT = 0, SHUT_RDWR = 2, SOL_SOCKET = 1, SO_SNDTIMEO = 2,
    tskIDLE_PRIORITY = 0;
#define pdMS_TO_TICKS(value) (value)

struct httpd_req_t {
  std::string query, status, body;
  std::map<std::string, std::string> headers;
  bool terminated = false, completed = false;
  int fd = 12;
};
struct FakeRing {
  struct Item { void* bytes; size_t size; bool complete = false; };
  size_t capacity = 8192, used = 0;
  std::deque<Item> items;
  std::map<void*, size_t> received;
  ~FakeRing() {
    for (const auto& item : items) free(item.bytes);
    for (const auto& item : received) free(item.first);
  }
};
using RingbufHandle_t = FakeRing*;
namespace fake {
  struct StopWorker {};
  struct StopTask {};
  int receivesRemaining = 1, yield = 1, scheduler = 1;
  int notifications = 0, shutdowns = 0, asyncCompletes = 0;
  int64_t monotonic = 1000, utc = 1900000000000LL;
  uint32_t random = 99;
  uint64_t nextId = 1;
  bool hasId = true, taskFailure = false, asyncFailure = false, sendFailure = false;
  bool queueFailure = false;
  bool nvsOpenFailure = false;
  bool rootReady = true;
  size_t rootTotal = 8 * 1024 * 1024;
  bool stepping = false;
  bool stepWithoutQueue = false;
  int waits = 0;
  size_t rootQueries = 0;
  std::string taskName = "origin-task", rootDirectory;
  std::map<std::string, std::function<void()>> tasks;
  std::map<std::string, int> priorities;
  std::vector<std::unique_ptr<FakeRing>> rings;
  std::function<void()> onSend;
  std::function<void()> onDelay;
  std::function<void()> afterReturn;
  std::function<void(esp32m::AppObject*)> onStateHint;
  struct StateHint { esp32m::AppObject* source; int64_t at; };
  std::vector<StateHint> stateHints;
  std::function<bool()> beforeFormat;
  std::function<void(bool)> afterFormat;
}
int64_t esp_timer_get_time() { return fake::monotonic * 1000; }
uint32_t esp_random() { return ++fake::random; }
int fake_gettimeofday(timeval* now, void*) {
  now->tv_sec = static_cast<long>(fake::utc / 1000);
  now->tv_usec = static_cast<long>(fake::utc % 1000) * 1000;
  return 0;
}
tm* fake_gmtime_r(const time_t* value, tm* result) {
  *result = *gmtime(value);
  return result;
}
size_t fake_strftime(char* output, size_t capacity, const char*, const tm* value) {
  return strftime(output, capacity, "%Y-%m-%d %H:%M:%S", value);
}
int xPortCanYield() { return fake::yield; }
int xTaskGetSchedulerState() { return fake::scheduler; }
TaskHandle_t xTaskGetCurrentTaskHandle() { return reinterpret_cast<void*>(1); }
const char* pcTaskGetName(TaskHandle_t) { return fake::taskName.c_str(); }
int xTaskCreate(void (*fn)(void*), const char* name, int, void* argument, int priority,
                TaskHandle_t* handle) {
  if (fake::taskFailure) return 0;
  fake::tasks[name] = [=]() { fn(argument); };
  fake::priorities[name] = priority;
  if (handle) *handle = reinterpret_cast<void*>(1);
  return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t) { ++fake::notifications; }
uint32_t ulTaskNotifyTake(int clear, int ticks) {
  assert(clear == pdTRUE && ticks > 0);
  if (fake::stepping && fake::receivesRemaining <= 0) throw fake::StopWorker{};
  if (fake::stepping && fake::stepWithoutQueue) --fake::receivesRemaining;
  ++fake::waits;
  fake::monotonic += ticks;
  return 0;
}
void vTaskDelay(int ticks) {
  fake::monotonic += ticks;
  if (fake::onDelay) fake::onDelay();
}
void vTaskDelete(TaskHandle_t) { throw fake::StopTask{}; }
RingbufHandle_t xRingbufferCreate(size_t bytes, int) {
  if (fake::queueFailure) return nullptr;
  auto ring = std::make_unique<FakeRing>();
  ring->capacity = bytes;
  auto* value = ring.get();
  fake::rings.push_back(std::move(ring));
  return value;
}
size_t xRingbufferGetMaxItemSize(RingbufHandle_t ring) { return ring->capacity / 2 - 8; }
int xRingbufferSendAcquire(RingbufHandle_t ring, void** item, size_t bytes, int ticks) {
  assert(ticks == 0);
  if (bytes > xRingbufferGetMaxItemSize(ring) || ring->used + bytes + 8 > ring->capacity)
    return 0;
  *item = malloc(bytes);
  ring->used += bytes + 8;
  ring->items.push_back({*item, bytes, false});
  return pdTRUE;
}
void xRingbufferSendComplete(RingbufHandle_t ring, void* item) {
  for (auto& queued : ring->items)
    if (queued.bytes == item) { queued.complete = true; return; }
  assert(false);
}
void* xRingbufferReceive(RingbufHandle_t ring, size_t* bytes, int ticks) {
  assert(ticks == 0);
  if (fake::stepping && fake::receivesRemaining-- <= 0) throw fake::StopWorker{};
  if (ring->items.empty() || !ring->items.front().complete) return nullptr;
  auto item = ring->items.front();
  ring->items.pop_front();
  ring->received[item.bytes] = item.size;
  *bytes = item.size;
  return item.bytes;
}
void vRingbufferReturnItem(RingbufHandle_t ring, void* item) {
  ring->used -= ring->received.at(item) + 8;
  ring->received.erase(item);
  free(item);
  if (fake::afterReturn) fake::afterReturn();
}
int nvs_open(const char*, int, nvs_handle_t* handle) {
  if (fake::nvsOpenFailure) return ESP_FAIL;
  *handle = 1;
  return ESP_OK;
}
int nvs_get_u64(nvs_handle_t, const char*, uint64_t* next) {
  *next = fake::nextId;
  return fake::hasId ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
int nvs_set_u64(nvs_handle_t, const char*, uint64_t next) {
  fake::nextId = next;
  fake::hasId = true;
  return ESP_OK;
}
int nvs_commit(nvs_handle_t) { return ESP_OK; }
void nvs_close(nvs_handle_t) {}
int shutdown(int, int) { ++fake::shutdowns; return 0; }
int setsockopt(int, int, int, const void*, size_t) { return 0; }
size_t httpd_req_get_url_query_len(httpd_req_t* req) { return req->query.size(); }
int httpd_req_get_url_query_str(httpd_req_t* req, char* value, size_t capacity) {
  if (req->query.size() >= capacity) return ESP_FAIL;
  memcpy(value, req->query.c_str(), req->query.size() + 1);
  return ESP_OK;
}
int httpd_query_key_value(const char* query, const char* key, char* value, size_t capacity) {
  std::string text(query), prefix = std::string(key) + "=";
  const auto pos = text.find(prefix);
  if (pos == std::string::npos) return ESP_FAIL;
  text = text.substr(pos + prefix.size());
  text = text.substr(0, text.find('&'));
  if (text.size() >= capacity) return ESP_FAIL;
  memcpy(value, text.c_str(), text.size() + 1);
  return ESP_OK;
}
int httpd_resp_send_err(httpd_req_t* req, int status, const char* text) {
  req->status = std::to_string(status); req->body = text; return ESP_OK;
}
int httpd_resp_set_status(httpd_req_t* req, const char* value) { req->status = value; return ESP_OK; }
int httpd_resp_sendstr(httpd_req_t* req, const char* value) { req->body += value; return ESP_OK; }
int httpd_resp_send_404(httpd_req_t* req) { req->status = "404"; return ESP_OK; }
int httpd_req_async_handler_begin(httpd_req_t* req, httpd_req_t** async) {
  if (fake::asyncFailure) return ESP_FAIL;
  *async = req; return ESP_OK;
}
int httpd_req_async_handler_complete(httpd_req_t* req) {
  req->completed = true; ++fake::asyncCompletes; return ESP_OK;
}
int httpd_req_to_sockfd(httpd_req_t* req) { return req->fd; }
int httpd_resp_set_type(httpd_req_t* req, const char* value) { req->headers["Content-Type"] = value; return ESP_OK; }
int httpd_resp_set_hdr(httpd_req_t* req, const char* key, const char* value) { req->headers[key] = value; return ESP_OK; }
int httpd_resp_send_chunk(httpd_req_t* req, const char* bytes, size_t length) {
  if (fake::onSend) fake::onSend();
  if (fake::sendFailure) return ESP_FAIL;
  if (!bytes && !length) req->terminated = true;
  else req->body.append(bytes, length);
  return ESP_OK;
}
size_t strlcpy(char* to, const char* from, size_t capacity) {
  const size_t length = strlen(from);
  if (capacity) { memcpy(to, from, std::min(length, capacity - 1)); to[std::min(length, capacity - 1)] = 0; }
  return length;
}

namespace esp32m {
  unsigned long millis() { return static_cast<unsigned long>(fake::monotonic); }
  struct Event { enum Kind { Init, Done, Sync, Periodic, Other } kind; };
  struct EventInit { static bool is(Event& event, int) { return event.kind == Event::Init; } };
  struct EventDone { static bool is(Event& event, void*) { return event.kind == Event::Done; } };
  struct EventPeriodic { static bool is(Event& event) { return event.kind == Event::Periodic; } };
  struct Request {
    std::string operation, error;
    bool internal = false;
    JsonDocument input, response;
    bool is(const char* name) const { return operation == name; }
    bool isInternal() const { return internal; }
    JsonVariantConst data() const { return input.as<JsonVariantConst>(); }
    void respondError(const char* text) { error = text; }
    void respond(JsonVariantConst value, bool) { response.set(value); }
  };
  struct RequestContext {
    Request& request;
    JsonVariantConst data;
    struct Errors {
      std::string message;
      void check(int, const char* text) { message = text; }
    } errors;
    RequestContext(Request& req) : request(req), data(req.data()) {}
  };
  class AppObject {
   public:
    virtual ~AppObject() = default;
    virtual const char* name() const = 0;
    virtual bool handleRequest(Request&) { return false; }
    virtual void handleEvent(Event&) {}
    virtual const JsonVariantConst descriptor() const { return {}; }
    virtual JsonDocument* getInfo(RequestContext&) { return nullptr; }
    virtual JsonDocument* getState(RequestContext&) { return nullptr; }
    virtual JsonDocument* getConfig(RequestContext&) { return nullptr; }
    virtual bool setConfig(RequestContext&) { return false; }
  };
  struct EventStateChanged {
    static void publish(AppObject* source) {
      // The production UI treats this as invalidation only; it captures the
      // source later on its task, after producer operations complete.
      fake::stateHints.push_back({source, fake::monotonic});
      if (fake::onStateHint) fake::onStateHint(source);
    }
  };
  struct App {
    static App& instance() { static App app; return app; }
    const char* name() const { return "host-app"; }
    const char* version() const { return "1.2.3"; }
  };
  namespace fs {
    struct RootInfo { std::string backend, label; size_t total = 0, used = 0; bool ready = false; };
    RootInfo rootInfo() {
      ++fake::rootQueries;
      if (!fake::rootReady) return {"host", "test", 0, 0, false};
      size_t used = 0;
      for (const auto& entry : fsys::directory_iterator(fake::rootDirectory))
        if (entry.is_regular_file()) used += entry.file_size();
      return {"host", "test", fake::rootTotal, used, true};
    }
    void setRootFormatHooks(std::function<bool()> before, std::function<void(bool)> after) {
      fake::beforeFormat = std::move(before); fake::afterFormat = std::move(after);
    }
  }
  namespace ui {
    struct IHttpHandler {
      IHttpHandler(const char*, int) {}
      virtual ~IHttpHandler() = default;
      virtual esp_err_t handle(httpd_req_t*) = 0;
    };
    struct Httpd {
      static Httpd* instance() { static Httpd self; return &self; }
      int addHttpHandler(IHttpHandler&) { return ESP_OK; }
    };
  }
  namespace net {
    bool otaRunning = false;
    namespace ota { bool isRunning() { return otaRunning; } }
    struct EventTimeSync { static bool is(Event& event) { return event.kind == Event::Sync; } };
  }
  namespace log {
    enum Level { None, Default, Error, Warning, Info, Debug, Verbose };
    struct LogMessage;
    class LogAppender {
     public:
      virtual ~LogAppender() = default;
      virtual bool append(const LogMessage*) = 0;
    };
    void addAppender(LogAppender*) {}
    char* format(const LogMessage*);
    using LogMessageFormatter = char* (*)(const LogMessage*);
    LogMessageFormatter formatter() { return format; }
  }
}

// Expose state only inside this host harness. Extracted production methods are
// otherwise unchanged; filesystem storage and ArduinoJson are the real code.
#define private public
#define protected public
#define gettimeofday fake_gettimeofday
#define gmtime_r fake_gmtime_r
#define strftime fake_strftime
#include "production.inc"
#undef strftime
#undef gmtime_r
#undef gettimeofday
#undef protected
#undef private

using namespace esp32m;
using namespace esp32m::log;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
std::string caseDirectory(const std::string& root, const char* name) {
  fake::rootReady = true;
  fake::rootTotal = 8 * 1024 * 1024;
  fake::queueFailure = false;
  fake::nvsOpenFailure = false;
  fake::onStateHint = {};
  fake::stateHints.clear();
  fake::rootDirectory = root + "/" + name;
  fsys::create_directory(fake::rootDirectory);
  return fake::rootDirectory;
}
void relocate(Store& store, const std::string& directory) {
  store._engine.~Engine();
  new (&store._engine) storage::Engine(directory, [&store]() { return store.reserveId(); });
}
void step(Store& store, int count = 1) {
  fake::receivesRemaining = count;
  fake::stepping = true;
  fake::stepWithoutQueue = !store._queue;
  try { store.run(); } catch (fake::StopWorker&) {}
  fake::stepping = false;
  fake::stepWithoutQueue = false;
}
bool configure(Store& store, bool enabled, size_t quota, uint32_t days = 0) {
  Request request;
  request.internal = true;
  request.input["enabled"] = enabled;
  request.input["quotaBytes"] = quota;
  request.input["maxAgeDays"] = days;
  RequestContext context(request);
  const bool changed = store.setConfig(context);
  require(context.errors.message.empty(), "valid config rejected");
  return changed;
}
bool configurePercent(Store& store, bool enabled, uint32_t percent,
                      bool internal = false) {
  Request request;
  request.internal = internal;
  request.input["enabled"] = enabled;
  request.input["quotaPercent"] = percent;
  request.input["maxAgeDays"] = 0;
  RequestContext context(request);
  const bool changed = store.setConfig(context);
  require(context.errors.message.empty(), "valid percentage config rejected");
  return changed;
}
std::unique_ptr<JsonDocument> configuration(Store& store) {
  Request request;
  RequestContext context(request);
  return std::unique_ptr<JsonDocument>(store.getConfig(context));
}
std::unique_ptr<JsonDocument> state(Store& store) {
  Request request;
  RequestContext context(request);
  return std::unique_ptr<JsonDocument>(store.getState(context));
}
std::unique_ptr<JsonDocument> information(Store& store) {
  Request request;
  RequestContext context(request);
  return std::unique_ptr<JsonDocument>(store.getInfo(context));
}
void foreignFile(const std::string& directory, size_t bytes) {
  std::ofstream file(directory + "/application-data.bin", std::ios::binary);
  if (bytes) { file.seekp(bytes - 1); file.put('x'); }
  file.close();
  require(bool(file), "create external storage fixture");
}
void start(Store& store) {
  Event init{Event::Init};
  store.handleEvent(init);
  step(store);
}
LogMessage* message(const std::string& text, const char* name = "origin") {
  return LogMessage::alloc(Level::Info, -fake::utc, name, text.c_str());
}
bool validUtf8(const std::string& text) {
  for (size_t i = 0; i < text.size();) {
    const auto c = static_cast<unsigned char>(text[i]);
    const size_t length = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
        (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
    if (!length || i + length > text.size()) return false;
    for (size_t j = 1; j < length; ++j)
      if ((static_cast<unsigned char>(text[i + j]) & 0xc0) != 0x80) return false;
    i += length;
  }
  return true;
}

int main(int argc, char** argv) {
  std::cout << std::unitbuf;
  require(argc == 2, "temporary directory missing");
  const std::string root = argv[1];
  {
    storage::Read page;
    page.text = std::string("valid ") + "\xd0\x96" + " malformed " +
        "\xff\xc0\x80\xed\xa0\x80\xf4\x90\x80\x80";
    page.offset = 17;
    page.size = page.offset + page.text.size();
    page.nextOffset = page.size;
    page.eof = true;
    const auto bytes = page.text.size();
    utf8Page(page);
    require(validUtf8(page.text) && page.text.find("\xd0\x96") != std::string::npos &&
            page.text.find("malformed ?") != std::string::npos &&
            page.text.size() <= bytes && page.offset == 17 && page.nextOffset == page.size,
            "malformed bytes broke bounded UTF-8 JSON page or byte cursors");
    page.text.assign(8192, static_cast<char>(0xff));
    page.offset = 0;
    page.size = page.nextOffset = 8192;
    utf8Page(page);
    require(page.text == std::string(8192, '?') && page.nextOffset == 8192,
            "malformed input expanded the hard page bound");
    std::cout << "bounded malformed UTF-8 display and byte cursors passed\n";
  }
  {
    const auto directory = caseDirectory(root, "quota-default-and-modes");
    Store store;
    relocate(store, directory);
    store.prepare();
    auto saved = configuration(store);
    require(store._policy.quotaPercent == 50 && (*saved)["quotaPercent"] == 50 &&
            (*saved)["quotaBytes"].as<size_t>() == fake::rootTotal / 2 &&
            !(*saved)["enabled"].as<bool>(),
            "fresh Store did not default to disabled capture and 50 percent");
    auto info = information(store);
    require((*info)["supportsQuotaPercent"] == true &&
            (*info)["quotaBaseBytes"].as<size_t>() == fake::rootTotal,
            "capability API omitted percentage support or quota basis");
    Request enable;
    enable.input["enabled"] = true;
    RequestContext enabling(enable);
    require(store.setConfig(enabling) && enabling.errors.message.empty() &&
            store._policy.quotaPercent == 50,
            "enabled-only update changed percentage mode");
    start(store);
    for (uint32_t percent = 1; percent <= 100; ++percent) {
      configurePercent(store, true, percent);
      require(store._policy.quotaPercent == percent,
              "valid 1..100 percentage was not applied");
    }
    step(store);
    require(store._capture, "valid full-space percentage failed to capture");
    require(store._engine.status().quotaBytes == fake::rootTotal &&
            store._engine.status().effectiveQuotaBytes ==
                fake::rootTotal - store.reserveLocked(),
            "100 percent quota bypassed the independent filesystem reserve");
    const auto before = store._policy;
    const auto notifications = fake::notifications;
    for (const char* value : {"-1", "0.5", "100.5", "101", "4294967296",
                              "null", "\"50\"", "false"}) {
      Request invalid;
      const auto json = std::string("{\"enabled\":false,\"maxAgeDays\":33,\"quotaPercent\":") + value + "}";
      require(!deserializeJson(invalid.input, json), "parse invalid-percent fixture");
      RequestContext context(invalid);
      require(!store.setConfig(context) && !context.errors.message.empty(),
              "invalid percentage accepted");
      require(store._enabled && store._capture &&
              store._policy.quotaPercent == before.quotaPercent &&
              store._policy.quotaBytes == before.quotaBytes &&
              store._policy.maxAgeDays == before.maxAgeDays &&
              fake::notifications == notifications,
              "invalid percentage partially changed policy or capture");
    }
    Request bytes;
    bytes.input["enabled"] = true;
    bytes.input["quotaPercent"] = 0;
    bytes.input["quotaBytes"] = 65536;
    RequestContext byteContext(bytes);
    require(store.setConfig(byteContext) && byteContext.errors.message.empty(),
            "explicit byte mode rejected");
    step(store);
    saved = configuration(store);
    require(store._policy.quotaPercent == 0 && (*saved)["quotaPercent"] == 0 &&
            (*saved)["quotaBytes"] == 65536,
            "switch from percentage to bytes did not round-trip");
    configurePercent(store, true, 50);
    step(store);
    require(bool(store._engine.flush()), "flush before dynamic quota fixture");
    const size_t external = 128 * 1024;
    foreignFile(directory, external);
    step(store);
    saved = configuration(store);
    auto status = state(store);
    info = information(store);
    const size_t base = fake::rootTotal - external;
    require((*saved)["quotaPercent"] == 50 &&
            (*saved)["quotaBytes"].as<size_t>() == base / 2 &&
            (*status)["quotaPercent"] == 50 &&
            (*status)["quotaBytes"].as<size_t>() == base / 2 &&
            (*status)["quotaBaseBytes"].as<size_t>() == base &&
            (*info)["quotaBaseBytes"].as<size_t>() == base,
            "config/state/info did not expose current resolved percentage budget");
    Request roundTrip;
    roundTrip.input.set(saved->as<JsonVariantConst>());
    // Resolved bytes are informational in percentage mode, even when the
    // capacity has changed since a client last loaded the settings.
    roundTrip.input["quotaBytes"] = 1;
    RequestContext same(roundTrip);
    const auto notifyBefore = fake::notifications;
    require(!store.setConfig(same) && same.errors.message.empty() &&
            store._policy.quotaPercent == 50 && store._capture &&
            fake::notifications == notifyBefore,
            "resolved-byte mirror altered or interrupted percentage policy");
    Request ageOnly;
    ageOnly.input["maxAgeDays"] = 12;
    RequestContext ageContext(ageOnly);
    require(store.setConfig(ageContext) && ageContext.errors.message.empty() &&
            store._policy.quotaPercent == 50,
            "retention-only update changed percentage mode");
    configure(store, true, 98304);
    require(store._policy.quotaPercent == 0 && store._policy.quotaBytes == 98304,
            "legacy quotaBytes update did not switch to absolute mode");
    std::cout << "50 percent default, integer validation, atomic rejection, mode switches and resolved config passed\n";
  }
  {
    const auto directory = caseDirectory(root, "quota-percent-restore");
    fake::rootTotal = 2 * 1024 * 1024;
    const size_t external = 512 * 1024;
    const size_t base = fake::rootTotal - external;
    foreignFile(directory, external);
    uint64_t id = 1000;
    storage::Engine previous(directory, [&]() { return id++; });
    require(bool(previous.configure({1024 * 1024, 0},
            {fake::rootTotal, external, 8192})), "prepare percent history");
    for (int i = 0; i < 200; ++i)
      require(bool(previous.append(std::string(3000, 'p'), 0, "previous",
          {fake::rootTotal, external + previous.status().usedBytes, 8192})),
          "create percent history");
    require(bool(previous.seal()), "seal percent history");
    const auto retained = previous.status().usedBytes;
    const auto first = previous.files().front().id;
    previous.reset();
    Store store;
    relocate(store, directory);
    store.prepare();
    // Before recovery the catalog does not yet know which occupied bytes are
    // its own. This fixture would be pruned if that temporary basis persisted.
    auto unresolved = configuration(store);
    require((*unresolved)["quotaBytes"].as<size_t>() < retained && retained < base / 2,
            "percent recovery fixture does not distinguish free-only quota");
    Request restored;
    restored.internal = true;
    restored.input["enabled"] = true;
    restored.input["quotaPercent"] = 50;
    restored.input["quotaBytes"] = base / 2;
    restored.input["maxAgeDays"] = 0;
    RequestContext restoreContext(restored);
    require(store.setConfig(restoreContext) && restoreContext.errors.message.empty(),
            "saved percentage policy rejected before recovery");
    start(store);
    require(store._capture && store._engine.status().usedBytes >= retained &&
            store._engine.files().front().id == first,
            "percentage recovery pruned history against a free-only budget");
    auto status = state(store);
    if ((*status)["quotaBaseBytes"].as<size_t>() != base ||
        (*status)["quotaBytes"].as<size_t>() != base / 2)
      std::cerr << "percentage recovery basis: expected=" << base
                << " actual=" << (*status)["quotaBaseBytes"].as<size_t>()
                << " resolved=" << (*status)["quotaBytes"].as<size_t>()
                << " owned=" << store._engine.status().usedBytes
                << " provider-used=" << store._root.used << '\n';
    require((*status)["quotaBaseBytes"].as<size_t>() == base &&
            (*status)["quotaBytes"].as<size_t>() == base / 2,
            "recovered own history reduced percentage basis");
    auto saved = configuration(store);
    Request roundTrip;
    roundTrip.internal = true;
    roundTrip.input.set(saved->as<JsonVariantConst>());
    RequestContext roundContext(roundTrip);
    require(!store.setConfig(roundContext) && roundContext.errors.message.empty() &&
            store._policy.quotaPercent == 50,
            "saved percentage config did not preserve mode on round-trip");
    int written = 0;
    fake::afterReturn = [&]() {
      // This callback runs while Store serializes the completed write. Call
      // the production accounting helpers directly rather than re-locking its
      // request methods. No periodic capacity refresh occurs within the burst.
      require(store._engine.status().quotaBaseBytes == base &&
              store._engine.status().quotaBytes == base / 2 &&
              store.quotaBaseLocked() == base && store.quotaBudgetLocked() == base / 2,
              "an unflushed queued write changed percentage basis or budget");
      ++written;
    };
    for (int i = 0; i < 6; ++i) {
      auto* record = message("percentage burst " + std::string(500, 'b'));
      require(store.append(record), "queue percentage burst");
      free(record);
    }
    step(store, 6);
    fake::afterReturn = {};
    require(written == 6 && store._bytesSinceFlush > 0 && store._bytesSinceFlush < 4096,
            "percentage burst did not exercise individually accounted unflushed writes");
    saved = configuration(store);
    require((*saved)["quotaBytes"].as<size_t>() == base / 2,
            "unflushed writes changed the saved resolved percentage budget");
    const auto usedBeforeLoss = store._engine.status().usedBytes;
    const auto countBeforeLoss = store._engine.files().size();
    fake::rootReady = false;
    step(store);
    require(!store._capture && store._policy.quotaPercent == 50 &&
            store._engine.status().usedBytes == usedBeforeLoss &&
            store._engine.files().size() == countBeforeLoss &&
            store._engine.files().front().id == first,
            "failed capacity query erased percent-owned history or policy");
    fake::rootReady = true;
    step(store);
    if (!store._capture || store._policy.quotaPercent != 50 ||
        store._engine.files().front().id != first ||
        store._engine.status().quotaBytes != base / 2)
      std::cerr << "percentage capacity restore: capture=" << store._capture
                << " expected=" << base / 2
                << " resolved=" << store._engine.status().quotaBytes
                << " baseline=" << store._engine.status().quotaBaseBytes
                << " owned=" << store._engine.status().usedBytes
                << " provider-used=" << store._root.used << '\n';
    require(store._capture && store._policy.quotaPercent == 50 &&
            store._engine.files().front().id == first &&
            store._engine.status().quotaBytes == base / 2,
            "percentage recovery after transient capacity loss changed basis");
    std::cout << "saved percentage policy restores before recovery without counting owned history as external passed\n";
  }
  {
    const auto directory = caseDirectory(root, "quota-percent-small");
    fake::rootTotal = 65536;
    Store store;
    relocate(store, directory);
    store.prepare();
    configurePercent(store, true, 1, true);  // Saved policy survives smaller storage.
    start(store);
    auto saved = configuration(store);
    auto status = state(store);
    require(store._policy.quotaPercent == 1 && store._enabled && !store._capture &&
            store._engine.files().empty() && (*saved)["quotaPercent"] == 1 &&
            (*saved)["quotaBytes"] == 655 && (*status)["quotaBytes"] == 655 &&
            !(*status)["ready"].as<bool>(),
            "subminimum percentage budget wrote a file or lost valid policy");
    Request restored;
    restored.internal = true;
    restored.input.set(saved->as<JsonVariantConst>());
    Store next;
    relocate(next, directory);
    next.prepare();
    RequestContext restoreContext(restored);
    require(next.setConfig(restoreContext) && restoreContext.errors.message.empty(),
            "resolved subminimum bytes invalidated saved percentage policy");
    start(next);
    require(next._enabled && next._policy.quotaPercent == 1 && !next._capture &&
            next._engine.files().empty(), "restored subminimum percentage wrote a segment");
    Request disable;
    disable.input["enabled"] = false;
    RequestContext disableContext(disable);
    require(next.setConfig(disableContext) && disableContext.errors.message.empty() &&
            !next._enabled && next._policy.quotaPercent == 1,
            "insufficient capacity prevented disabling capture");
    Request enableSmall;
    enableSmall.input["enabled"] = true;
    RequestContext enableSmallContext(enableSmall);
    require(!next.setConfig(enableSmallContext) &&
            !enableSmallContext.errors.message.empty() && !next._enabled &&
            next._policy.quotaPercent == 1 && next._engine.files().empty(),
            "external enable accepted a subminimum effective budget");
    fake::rootTotal = 8 * 1024 * 1024;
    configurePercent(next, true, 1);
    step(next);
    require(next._capture && next._policy.quotaPercent == 1,
            "percentage capture did not resume when capacity increased");
    fake::rootTotal = 65536;
    step(next);
    require(!next._capture && next._policy.quotaPercent == 1 && next._enabled,
            "capacity shrink did not pause percentage capture");
    saved = configuration(next);
    require((*saved)["quotaPercent"] == 1 && (*saved)["quotaBytes"] == 655,
            "capacity shrink wrote an invalid fixed-byte policy");
    std::cout << "small percentage budgets pause, restore, disable and resume without invalid byte policy passed\n";
  }
  {
    const auto directory = caseDirectory(root, "producer-drain");
    Store store;
    relocate(store, directory);
    store.prepare();
    configure(store, true, 65536);
    start(store);
    auto* original = message("in-flight accepted record");
    const size_t bytes = sizeof(Store::Queued) + original->size();
    void* reserved = nullptr;
    require(xRingbufferSendAcquire(store._queue, &reserved, bytes, 0) == pdTRUE,
            "reserve simulated in-flight producer");
    store._producers = 1;
    bool published = false;
    fake::onDelay = [&]() {
      if (published) return;
      auto* queued = static_cast<Store::Queued*>(reserved);
      queued->truncated = false;
      queued->valid = original->copyTo(queued + 1, bytes - sizeof(Store::Queued)) != nullptr;
      store._pending.fetch_add(bytes);
      xRingbufferSendComplete(store._queue, reserved);
      store._producers = 0;
      published = true;
    };
    configure(store, false, 65536);
    fake::onDelay = {};
    free(original);
    storage::Read drained;
    require(published && store._pending == 0 && store._producers == 0 &&
            bool(store._engine.read(store._engine.files().back().id, 0, 8192, drained)) &&
            drained.text.find("in-flight accepted record") != std::string::npos,
            "quiesce lost producer that published after capture was disabled");
    std::cout << "in-flight producer completion drains before graceful seal passed\n";
  }
  {
    const auto directory = caseDirectory(root, "capacity-clock");
    Store store;
    relocate(store, directory);
    store.prepare();
    configure(store, true, 65536);
    start(store);
    require(fake::priorities.at("m/log-store") == tskIDLE_PRIORITY,
            "saturated worker can preempt idle-priority App/UI tasks");
    auto* original = message("must survive failed capacity query");
    store.append(original); free(original); step(store);
    const auto before = store._engine.status().usedBytes;
    const auto files = store._engine.files().size();
    const auto first = store._engine.files().front().id;
    fake::rootReady = false;
    step(store);
    require(!store._capture && store._engine.status().usedBytes == before &&
            store._engine.files().size() == files && store._engine.files().front().id == first,
            "failed capacity query erased or altered retained logs");
    fake::rootReady = true;
    step(store);
    require(store._capture && store._engine.status().usedBytes >= before &&
            store._engine.files().front().id == first,
            "capture did not recover after capacity query restored");
    store.timeSynchronized();
    require(store.trustedNowLocked() == 0, "clock skipped stability interval");
    for (int i = 0; i < 4; ++i) {
      fake::monotonic += 30000;
      fake::utc += 30000;
      store.timeSynchronized();
    }
    require(store.trustedNowLocked() == fake::utc, "frequent stable synchronization postponed retention forever");
    fake::utc += 2 * 86400000LL;
    require(store.trustedNowLocked() == 0 && !store._clockTrusted,
            "unsignaled clock jump remained trusted");
    fake::monotonic += 120000;
    fake::utc += 120000;
    require(store.trustedNowLocked() == 0, "clock jump self-revalidated without explicit sync");
    store.timeSynchronized();
    fake::monotonic += 60000;
    fake::utc += 60000;
    require(store.trustedNowLocked() == fake::utc, "explicit sync failed to revalidate after stability");
    std::cout << "transient capacity loss preserves history and stable sync/jump trust passed\n";
  }
  {
    const auto directory = caseDirectory(root, "restore");
    uint64_t id = 10;
    storage::Engine previous(directory, [&]() { return id++; });
    require(bool(previous.configure({1024 * 1024, 0}, {8 * 1024 * 1024, 0, 8192})), "prepare history");
    for (int i = 0; i < 200; ++i)
      require(bool(previous.append(std::string(1900, 'h'), fake::utc, "previous",
          {8 * 1024 * 1024, previous.status().usedBytes, 8192})), "create history");
    require(bool(previous.seal()), "seal history");
    const size_t retained = previous.status().usedBytes;
    require(retained > 256 * 1024, "fixture must exceed default quota");
    previous.reset();
    Store store;
    relocate(store, directory);
    store.prepare();
    configure(store, true, 1024 * 1024);
    start(store);
    require(store._engine.status().usedBytes >= retained, "saved quota applied after destructive recovery");
    const auto legacy = configuration(store);
    require(store._policy.quotaPercent == 0 && (*legacy)["quotaPercent"] == 0 &&
            (*legacy)["quotaBytes"].as<size_t>() == 1024 * 1024 &&
            store._engine.status().quotaBytes == 1024 * 1024,
            "legacy saved byte quota changed to default percentage mode");
    require(store._engine.files().size() > 8, "history lost at restart");
    require(store._capture, "enabled restored capture did not start");
    require(store.reserveId() > id, "NVS ID did not advance above recovered catalog");
    std::cout << "saved policy before recovery and durable ID floor passed\n";
  }
  {
    const auto directory = caseDirectory(root, "queue");
    Store store;
    relocate(store, directory);
    store.prepare();
    configure(store, true, 65536);
    start(store);
    std::string text;
    for (int i = 0; i < 1600; ++i) text += "\xd0\x96";
    auto* original = message(text);
    const auto diskBefore = store._engine.status().usedBytes;
    store._mutex.lock();
    require(store.append(original), "private capture append failed");
    store._mutex.unlock();
    require(store._engine.status().usedBytes == diskBefore, "capture caller performed storage I/O");
    require(store._pending > 0 && store._pending <= Store::ItemBytes, "queue item was not bounded");
    const auto* queued = static_cast<Store::Queued*>(store._queue->items.front().bytes);
    const auto* copy = reinterpret_cast<const LogMessage*>(queued + 1);
    require(std::string(copy->task()) == "origin-task", "original task metadata replaced by worker");
    require(std::string(copy->name()) == "origin", "logger name not preserved");
    require(copy->stamp() == original->stamp(), "original timestamp not preserved");
    require(validUtf8(copy->message()), "queue truncation broke UTF-8");
    require(queued->truncated && store._truncated == 1, "truncation counter missing");
    free(original);
    step(store);
    require(store._pending == 0, "pending bytes leaked after writing");
    require(!store._engine.files().empty(), "queued record not stored");
    storage::Read out;
    require(bool(store._engine.read(store._engine.files().back().id, 0, 8192, out)), "read stored record");
    require(out.text.find("[origin-task] origin") != std::string::npos, "stored metadata changed");
    require(out.text.find("[truncated]") != std::string::npos, "stored truncation marker absent");
    original = message(std::string(2000, 'x'));
    for (int i = 0; i < 12; ++i) store.append(original);
    require(store._dropped > 0, "full private queue did not count drops");
    require(store._queue->used <= Store::QueueBytes, "private queue exceeded bound");
    free(original);
    const auto before = store._dropped.load();
    fake::scheduler = taskSCHEDULER_SUSPENDED;
    original = message("critical section");
    require(!store.append(original), "suspended scheduler was allowed into queue");
    require(store._dropped == before + 1, "suspended drop not counted");
    fake::scheduler = 1;
    free(original);
    // Exercise every possible cut around a multibyte message, using the actual
    // production bounded-copy method rather than a separate truncation model.
    original = message("abc\xf0\x9f\x8c\xbb\xd0\x96xyz");
    const auto overhead = sizeof(LogMessage) + original->tasklen() + 1 + original->namelen() + 1;
    for (size_t room = 1; room <= original->messagelen(); ++room) {
      std::vector<unsigned char> pool(overhead + room);
      bool truncated = false;
      const auto* copy = original->copyTo(pool.data(), pool.size(), &truncated);
      require(copy && copy->size() <= pool.size() && validUtf8(copy->message()), "UTF-8 cut boundary failed");
      require(copy->level() == original->level() && copy->stamp() == original->stamp(), "copy changed metadata");
    }
    free(original);
    std::cout << "nonblocking bounded capture, metadata, UTF-8 and queue loss passed\n";
  }
  {
    const auto directory = caseDirectory(root, "config-api");
    Store store;
    relocate(store, directory);
    store.prepare();
    configure(store, true, 65536);
    start(store);
    Request bad;
    bad.input["quotaBytes"] = -1;
    RequestContext invalid(bad);
    require(!store.setConfig(invalid) && !invalid.errors.message.empty(), "negative quota accepted");
    require(store._policy.quotaBytes == 65536 && store._enabled, "invalid config partially applied");
    auto* original = message("API record");
    store.append(original);
    free(original);
    step(store);
    Request list;
    list.operation = "list";
    require(store.handleRequest(list), "list unhandled");
    require(list.error.empty() && list.response["files"].size() == 1, "list did not return catalog");
    const std::string id = list.response["files"][0]["id"].as<std::string>();
    const std::string generation = list.response["generation"].as<std::string>();
    Request read;
    read.operation = "read";
    read.input["id"] = id;
    read.input["generation"] = generation;
    read.input["tail"] = true;
    require(store.handleRequest(read) && read.error.empty(), "bounded tail read failed");
    require(read.response["text"].as<std::string>().find("API record") != std::string::npos, "read missing text");
    read.input["generation"] = "old-generation";
    read.error.clear();
    store.handleRequest(read);
    require(read.error == "LOGS_STALE", "stale generation accepted");
    read.input["generation"] = generation;
    read.input["id"] = "../config.json";
    read.error.clear();
    store.handleRequest(read);
    require(read.error == "LOGS_INVALID_ID", "arbitrary read path accepted");
    read.input["id"] = id;
    read.input["offset"] = -1;
    read.error.clear();
    store.handleRequest(read);
    require(read.error == "LOGS_INVALID_CURSOR", "negative byte offset accepted");
    read.input["offset"] = 0;
    read.input["limit"] = 8193;
    read.error.clear();
    store.handleRequest(read);
    require(read.error == "LOGS_INVALID_LIMIT", "oversized read accepted");
    original = message("accepted before disable");
    require(store.append(original), "enqueue before disable");
    free(original);
    configure(store, false, 65536);
    require(!store._capture && !store._engine.files().back().active, "disable failed to seal active file");
    require(!store._engine.files().empty(), "disable deleted retained history");
    storage::Read drained;
    require(store._pending == 0 &&
            bool(store._engine.read(store._engine.files().back().id, 0, 8192, drained)) &&
            drained.text.find("accepted before disable") != std::string::npos,
            "disable sealed before draining accepted records");
    require(store.beforeFormat(), "quiescent format refused");
    require(!store._ready && store._engine.files().empty(), "before-format retained open catalog");
    store.afterFormat(true);
    step(store);
    require(store._ready && !store._suspended, "after-format did not recover catalog");
    configure(store, true, 65536);
    step(store);
    net::otaRunning = true;
    step(store);
    require(!store._capture && store._otaPaused &&
            (store._engine.files().empty() || !store._engine.files().back().active),
            "OTA did not pause and seal storage");
    net::otaRunning = false;
    step(store);
    require(store._capture && !store._otaPaused, "capture failed to resume after OTA");
    original = message("accepted before shutdown");
    require(store.append(original), "enqueue before shutdown");
    free(original);
    Event done{Event::Done};
    store.handleEvent(done);
    require(!store._capture && store._suspended, "shutdown did not stop capture");
    require(store._pending == 0 &&
            bool(store._engine.read(store._engine.files().back().id, 0, 8192, drained)) &&
            drained.text.find("accepted before shutdown") != std::string::npos,
            "shutdown sealed before draining accepted records");
    std::cout << "config validation, catalog/cursor API, disable and format lifecycle passed\n";
  }
  {
    const auto directory = caseDirectory(root, "export");
    Store store;
    relocate(store, directory);
    store.prepare();
    configure(store, true, 65536);
    start(store);
    auto* original = message(std::string(1800, 'e'));
    store.append(original); free(original); step(store);
    const auto id = store._engine.files().front().id;
    storage::Read page;
    require(bool(store._engine.read(id, 0, 8192, page)), "expected export read");
    httpd_req_t request;
    request.query = "id=" + id;
    store.handle(&request);
    require(store._exportActive && store._engine.hasSnapshots(), "export did not pin snapshot");
    Request busy;
    busy.operation = "read";
    busy.input["id"] = id;
    store.handleRequest(busy);
    require(busy.error == "LOGS_BUSY", "browse was not deferred during export");
    try { fake::tasks.at("m/log-export")(); } catch (fake::StopTask&) {}
    require(request.completed && request.terminated && request.body == page.text, "export bytes/normal completion mismatch");
    require(!store._exportActive && !store._engine.hasSnapshots() && store._exportFd == -1, "successful export leaked resources");
    httpd_req_t canceled;
    canceled.query = "id=" + id;
    store.handle(&canceled);
    const auto shutdowns = fake::shutdowns;
    store.cancelExportLocked();
    try { fake::tasks.at("m/log-export")(); } catch (fake::StopTask&) {}
    require(canceled.completed && !canceled.terminated, "canceled export ended as successful download");
    require(fake::shutdowns > shutdowns && !store._engine.hasSnapshots() && !store._exportActive,
            "export cancellation leaked pins/socket state");
    httpd_req_t stalled;
    stalled.query = "id=" + id;
    store.handle(&stalled);
    const auto previousShutdowns = fake::shutdowns;
    fake::monotonic = store._exportDeadline + 1;
    step(store);
    require(store._exportCancel && fake::shutdowns > previousShutdowns,
            "independent worker did not interrupt stalled export at total deadline");
    try { fake::tasks.at("m/log-export")(); } catch (fake::StopTask&) {}
    require(stalled.completed && !stalled.terminated && !store._engine.hasSnapshots() &&
            !store._exportActive, "watchdog-canceled export leaked pins or completed normally");
    httpd_req_t failed;
    failed.query = "id=" + id;
    fake::taskFailure = true;
    store.handle(&failed);
    fake::taskFailure = false;
    require(failed.completed && !store._engine.hasSnapshots() && !store._exportActive,
            "worker creation failure leaked export state");
    httpd_req_t asyncFailed;
    asyncFailed.query = "id=" + id;
    fake::asyncFailure = true;
    store.handle(&asyncFailed);
    fake::asyncFailure = false;
    require(!store._engine.hasSnapshots() && !store._exportActive,
            "async request failure leaked snapshot");
    httpd_req_t sendFailed;
    sendFailed.query = "id=" + id;
    store.handle(&sendFailed);
    fake::sendFailure = true;
    try { fake::tasks.at("m/log-export")(); } catch (fake::StopTask&) {}
    fake::sendFailure = false;
    require(sendFailed.completed && !sendFailed.terminated && !store._engine.hasSnapshots(),
            "network failure produced successful download or leaked pin");
    httpd_req_t timedOut;
    timedOut.query = "id=" + id;
    store.handle(&timedOut);
    fake::onSend = []() { fake::monotonic += 120001; };
    try { fake::tasks.at("m/log-export")(); } catch (fake::StopTask&) {}
    fake::onSend = {};
    require(timedOut.completed && !timedOut.terminated && !store._engine.hasSnapshots(),
            "timeout produced successful download or leaked pin");
    httpd_req_t invalid;
    invalid.query = "id=../../config.json";
    store.handle(&invalid);
    require(invalid.status == "400" && !store._exportActive, "arbitrary export path accepted");
    // Export all files is the empty-query route. Force a second sealed segment,
    // then check both IDs are delimited and cleanup is identical.
    require(bool(store._engine.seal()), "seal for combined export");
    require(bool(store._engine.append("second segment", 0, store._bootId, store.capacityLocked())),
            "append second segment");
    httpd_req_t combined;
    store.handle(&combined);
    try { fake::tasks.at("m/log-export")(); } catch (fake::StopTask&) {}
    require(combined.completed && combined.terminated &&
            combined.body.find("# ===== " + id) != std::string::npos &&
            combined.body.find("second segment") != std::string::npos,
            "combined export missing segment boundaries/text");
    require(!store._engine.hasSnapshots() && !store._exportActive, "combined export leaked resources");
    std::cout << "streamed snapshot equality, cancellation and worker-failure cleanup passed\n";
  }
  {
    const auto directory = caseDirectory(root, "state-hints");
    Store store;
    relocate(store, directory);
    store.prepare();
    configure(store, true, 65536);
    start(store);
    step(store, 30);  // Settle the boot marker, capacity sample and telemetry.
    store.publishStateChanged();
    fake::stateHints.clear();
    std::vector<JsonDocument> captured;
    fake::onStateHint = [&](AppObject* source) {
      require(source == static_cast<AppObject*>(&store), "state hint named wrong source");
      const auto queries = fake::rootQueries;
      // A stronger regression than the normal deferred UI consumer: a hint
      // callback can capture immediately, so publishing under Store's mutex
      // deadlocks and fails the runner's bounded process timeout.
      captured.push_back(*state(store));
      require(fake::rootQueries == queries, "state hint capture queried filesystem");
    };
    step(store, 25);
    require(fake::stateHints.empty(), "unchanged healthy idle state flooded hints");
    for (int i = 0; i < 20; ++i) {
      const auto snapshot = state(store);
      const auto settings = configuration(store);
      const auto info = information(store);
      require((*snapshot)["ready"].as<bool>() && (*settings)["enabled"].as<bool>() &&
              (*info)["available"].as<bool>(), "read-only state fixture unavailable");
      store.publishStateChanged();
    }
    require(fake::stateHints.empty(), "state/info/config reads invalidated themselves");
    const auto usedBefore = store._engine.status().usedBytes;
    for (int i = 0; i < 10; ++i) {
      auto* record = message("pubsub burst " + std::string(100, 's'));
      require(store.append(record), "queue state hint burst");
      free(record);
    }
    require(fake::stateHints.empty(), "logging caller published state inline");
    step(store, 10);
    require(fake::stateHints.size() == 1 && captured.size() == 1 &&
            store._engine.status().usedBytes > usedBefore,
            "same-time write burst was not coalesced to one telemetry hint");
    const auto firstAt = fake::stateHints.front().at;
    for (int i = 0; i < 50; ++i) store.publishStateChanged();
    require(fake::stateHints.size() == 1, "telemetry published repeatedly without elapsed bound");
    fake::monotonic = firstAt + 999;
    store.publishStateChanged();
    require(fake::stateHints.size() == 1, "telemetry bypassed one-second bound");
    fake::monotonic = firstAt + 1000;
    store.publishStateChanged();
    require(fake::stateHints.size() == 2 &&
            captured.back()["usedBytes"].as<size_t>() == store._engine.status().usedBytes &&
            captured.back()["pendingBytes"] == 0,
            "coalesced hint tail failed to expose final stored and pending bytes");
    for (int i = 0; i < 50; ++i) store.publishStateChanged();
    require(fake::stateHints.size() == 2, "unchanged state published after consuming dirty telemetry");

    step(store, 25);
    store.publishStateChanged();
    fake::stateHints.clear();
    captured.clear();
    fake::monotonic += 1000;
    const auto droppedBefore = store._dropped.load();
    auto* record = message("suspended producer");
    fake::scheduler = taskSCHEDULER_SUSPENDED;
    require(!store.append(record), "suspended producer was not rejected");
    fake::scheduler = 1;
    free(record);
    require(fake::stateHints.empty(), "drop counter published from suspended logging caller");
    step(store);
    require(fake::stateHints.size() == 1 &&
            captured.back()["dropped"].as<uint32_t>() == droppedBefore + 1,
            "asynchronous dropped-record counter did not invalidate state");

    fake::stateHints.clear();
    captured.clear();
    fake::rootReady = false;
    step(store);
    require(!captured.empty() && !captured.back()["available"].as<bool>() &&
            !captured.back()["ready"].as<bool>() &&
            !captured.back()["lastError"].as<std::string>().empty(),
            "capacity failure did not publish paused/error state promptly");
    fake::stateHints.clear();
    captured.clear();
    step(store, 25);
    require(fake::stateHints.empty(), "unchanged unavailable/error state flooded hints");
    fake::rootReady = true;
    step(store);
    require(!captured.empty() && captured.back()["available"].as<bool>() &&
            captured.back()["ready"].as<bool>() &&
            captured.back()["lastError"].as<std::string>().empty(),
            "capacity recovery did not publish resumed state promptly");

    fake::stateHints.clear();
    captured.clear();
    httpd_req_t exporting;
    exporting.query = "id=" + store._engine.files().front().id;
    store.handle(&exporting);
    step(store);
    require(!captured.empty() && captured.back()["exporting"].as<bool>(),
            "HTTP export start did not invalidate subscribed state");
    fake::stateHints.clear();
    captured.clear();
    try { fake::tasks.at("m/log-export")(); } catch (fake::StopTask&) {}
    step(store);
    require(!captured.empty() && !captured.back()["exporting"].as<bool>() &&
            !store._exportActive && exporting.completed,
            "asynchronous HTTP export completion did not invalidate state");

    fake::stateHints.clear();
    captured.clear();
    configure(store, false, 65536, 12);
    step(store);
    require(!captured.empty() && !captured.back()["enabled"].as<bool>() &&
            captured.back()["maxAgeDays"] == 12 &&
            !captured.back()["catalogGeneration"].as<std::string>().empty(),
            "configuration/retention/catalog state did not invalidate");
    fake::stateHints.clear();
    captured.clear();
    require(store.beforeFormat(), "pubsub format fixture could not quiesce");
    step(store);
    require(!captured.empty() && !captured.back()["ready"].as<bool>() &&
            captured.back()["usedBytes"] == 0,
            "filesystem quiesce/reset did not invalidate state");
    fake::stateHints.clear();
    captured.clear();
    store.afterFormat(true);
    step(store);
    require(!captured.empty() && captured.back()["ready"].as<bool>(),
            "filesystem recovery did not invalidate restored state");
    store.timeSynchronized();
    store.publishStateChanged();
    fake::stateHints.clear();
    captured.clear();
    fake::monotonic += 60000;
    fake::utc += 60000;
    step(store);
    require(!captured.empty() && captured.back()["timeValid"].as<bool>(),
            "cached clock validity transition did not invalidate state");
    fake::onStateHint = {};
    std::cout << "bounded Store pubsub invalidation, urgent lifecycle changes, no-op suppression and synchronous capture passed\n";
  }
  for (const int failure : {0, 1, 2}) {
    const auto directory = caseDirectory(root, failure == 1 ?
        "state-hints-missing-queue" : failure == 2 ?
        "state-hints-marker-failure" : "state-hints-paused-budget");
    if (failure == 0) fake::rootTotal = 65536;
    fake::queueFailure = failure == 1;
    fake::nvsOpenFailure = failure == 2;
    Store store;
    fake::queueFailure = false;
    relocate(store, directory);
    store.prepare();
    if (failure) configure(store, true, 65536);
    else configurePercent(store, true, 1, true);
    start(store);
    step(store, 25);
    store.publishStateChanged();
    fake::stateHints.clear();
    fake::onStateHint = [&](AppObject* source) {
      require(source == static_cast<AppObject*>(&store), "paused hint named wrong source");
      const auto snapshot = state(store);
      require(!(*snapshot)["ready"].as<bool>() &&
              !(*snapshot)["lastError"].as<std::string>().empty(),
              "paused storage hint exposed transient cleared error");
    };
    step(store, 25);
    require(fake::stateHints.empty(), failure == 1 ?
        "unchanged queue allocation error flooded hints" : failure == 2 ?
        "unchanged application-marker failure flooded hints" :
        "unchanged below-minimum quota error flooded hints");
    if (failure == 2) {
      bool recovered = false;
      fake::onStateHint = [&](AppObject* source) {
        require(source == static_cast<AppObject*>(&store), "marker recovery hint named wrong source");
        const auto snapshot = state(store);
        recovered = (*snapshot)["ready"].as<bool>() &&
                    (*snapshot)["lastError"].as<std::string>().empty();
      };
      fake::nvsOpenFailure = false;
      step(store);
      require(recovered && store._capture && store._sessionMarked,
              "application-marker recovery did not publish resumed healthy state");
    }
    fake::onStateHint = {};
    fake::nvsOpenFailure = false;
    std::cout << (failure == 1 ? "missing queue" : failure == 2 ? "marker failure/recovery" : "paused quota")
              << " state avoids repeated unchanged error invalidation passed\n";
  }
  {
    const auto directory = caseDirectory(root, "state-hints-without-worker");
    Store store;
    relocate(store, directory);
    store.prepare();
    configure(store, true, 65536);
    fake::taskFailure = true;
    Event init{Event::Init};
    store.handleEvent(init);
    fake::taskFailure = false;
    require(!store._task, "worker-failure pubsub fixture unexpectedly started worker");
    fake::stateHints.clear();
    std::vector<JsonDocument> captured;
    fake::onStateHint = [&](AppObject* source) {
      require(source == static_cast<AppObject*>(&store), "fallback hint named wrong source");
      captured.push_back(*state(store));
    };
    Event periodic{Event::Periodic};
    store.handleEvent(periodic);
    require(captured.size() == 1 && !captured.back()["ready"].as<bool>() &&
            !captured.back()["lastError"].as<std::string>().empty(),
            "worker allocation failure did not publish through periodic fallback");
    for (int i = 0; i < 25; ++i) {
      fake::monotonic += 100;
      store.handleEvent(periodic);
    }
    require(captured.size() == 1, "unchanged worker failure flooded periodic hints");
    auto* record = message("dropped while worker unavailable");
    store.append(record);
    free(record);
    require(captured.size() == 1, "failed-worker drop published from logging caller");
    fake::monotonic += 1000;
    store.handleEvent(periodic);
    require(captured.size() == 2 && captured.back()["dropped"] == 1,
            "periodic fallback did not publish worker-unavailable drop counter");
    fake::onStateHint = {};
    std::cout << "Store pubsub periodic fallback without worker and cached snapshot reentry passed\n";
  }
}
