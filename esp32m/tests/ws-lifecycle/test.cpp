#include <ArduinoJson.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include "esp32m/ui/httpd-access.hpp"

#define CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT 1
#define CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT 1
#define CONFIG_ESP32M_UI_TASK_STACK_SIZE 8192
#define ESP_CHECK_RETURN(expression) do { const auto error = (expression); if (error != ESP_OK) return error; } while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(expression) (expression)

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = 259,
              ESP_ERR_NO_MEM = 257, ESP_ERR_INVALID_SIZE = 260, SHUT_RDWR = 2;
using httpd_handle_t = void*;
constexpr int HTTP_GET = 1;
struct httpd_req_t {
  int method = HTTP_GET, fd = 7;
  void* user_ctx = nullptr;
  const char* uri = "/ws";
  std::string payload;
};
struct httpd_uri_t {
  esp_err_t (*handler)(httpd_req_t*) = nullptr;
  esp_err_t (*ws_pre_handshake_cb)(httpd_req_t*) = nullptr;
  esp_err_t (*ws_post_handshake_cb)(httpd_req_t*) = nullptr;
};
struct httpd_ws_frame_t { uint8_t* payload; size_t len; int type; };
constexpr int HTTPD_WS_TYPE_TEXT = 1;
int httpd_req_to_sockfd(httpd_req_t* request) { return request->fd; }
int httpd_ws_recv_frame(httpd_req_t* request, httpd_ws_frame_t* frame, size_t max) {
  frame->len = request->payload.size(); frame->type = HTTPD_WS_TYPE_TEXT;
  if (max) memcpy(frame->payload, request->payload.data(), frame->len);
  return ESP_OK;
}
namespace esp32m { class Request; }
namespace fake {
  struct Work { void (*callback)(void*); void* argument; };
  std::deque<Work> queue;
  size_t capacity = 1000;
  bool sendFails = false;
  std::vector<std::pair<int, std::string>> frames;
  std::vector<int> shutdowns;
  std::function<void(esp32m::Request&)> dispatch;
  void drain() {
    while (!queue.empty()) {
      auto item = queue.front(); queue.pop_front(); item.callback(item.argument);
    }
  }
  void reset() { assert(queue.empty()); frames.clear(); shutdowns.clear(); capacity = 1000; sendFails = false; dispatch = {}; }
}
int httpd_queue_work(httpd_handle_t, void (*callback)(void*), void* argument) {
  if (fake::queue.size() >= fake::capacity) return ESP_FAIL;
  fake::queue.push_back({callback, argument}); return ESP_OK;
}
int httpd_ws_send_frame_async(httpd_handle_t, int cid, httpd_ws_frame_t* packet) {
  if (fake::sendFails) return ESP_FAIL;
  fake::frames.emplace_back(cid, std::string((char*)packet->payload, packet->len)); return ESP_OK;
}
int shutdown(int cid, int) { fake::shutdowns.push_back(cid); return 0; }
int esp_task_wdt_reset() { return 0; }
unsigned uxTaskGetStackHighWaterMark(void*) { return 3072; }
int64_t esp_timer_get_time() { return (int64_t(1) << 40) * 1000; }
#define LOGD(...) ((void)0)

namespace esp32m {
  unsigned long millis() { return 1000; }
  std::string string_printf(const char* format, ...) {
    char buffer[64]; va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); return buffer;
  }
  namespace log {
    struct Loggable {
      virtual ~Loggable() = default;
      virtual const char* name() const { return "test"; }
      template <typename... T> void logW(const char*, T...) {}
      template <typename... T> void logI(const char*, T...) {}
    };
  }
  namespace json {
    template <typename T> T null() { return T{}; }
    JsonDocument* parse(const char* text, size_t size) {
      auto result = new JsonDocument();
      if (deserializeJson(*result, text, size)) { delete result; return nullptr; }
      return result;
    }
  }
  class Response;
  class Request {
   public:
    virtual ~Request() = default;
    const char* name() const { return _name; }
    int seq() const { return _seq; }
    const char* target() const { return _target; }
    void publish() { if (fake::dispatch) fake::dispatch(*this); }
    void respond(const char* source, JsonVariantConst data, bool error) { respondImpl(source, data, error); }
    void respond() { respondImpl(_target, JsonVariantConst{}, false); }
    Response* makeResponse() { return makeResponseImpl(); }
   protected:
    static constexpr const char* Type = "request";
    Request(const char* name, int seq, const char* target, JsonVariantConst, const char*)
      : _name(name), _seq(seq), _target(target) {}
    virtual void respondImpl(const char*, JsonVariantConst, bool) = 0;
    virtual Response* makeResponseImpl() = 0;
   private:
    const char* _name; int _seq; const char* _target;
  };
  class Response {
   public:
    Response(const char*, const Request&, const char*) {}
    virtual ~Response() = default;
  };
  namespace ui {
    class Transport;
    struct MockSubscriptions {
      struct Session { Transport* transport; uint32_t cid; uint64_t generation; };
      bool request(Session, const char*, int, JsonVariantConst, uint32_t) { return false; }
      void stats(JsonObject) {}
    };
  }
  struct Ui {
    ui::MockSubscriptions _subscriptions;
    uint64_t _wsStateReads = 0, _wsDeviceReads = 0;
    unsigned notifications = 0;
    void notifyIncoming() { ++notifications; }
    void process(ui::Transport& transport);
  };
}

#include "production.inc"

namespace esp32m::ui {
  // The HTTP server/socket platform fields are replaced; all send/close work
  // methods below are compiled unchanged from production Httpd.
  class Httpd : public Transport {
   public:
    explicit Httpd(Ui& ui) { init(&ui); }
    const char* name() const override { return "ui-http"; }
    uint64_t open(uint32_t cid) {
      auto generation = sessionOpened(cid);
      std::lock_guard lock(_wsSessionMutex); _controlSessions[cid] = generation;
      return generation;
    }
    void physicalClose(uint32_t cid) {
      { std::lock_guard lock(_wsSessionMutex); _controlSessions.erase(cid); }
      sessionClosed(cid);
    }
    void receive(uint32_t cid, const char* text, uint64_t generation) {
      incoming(cid, (void*)text, strlen(text), generation);
    }
    esp_err_t sendTo(uint32_t, const char*) override;
    esp_err_t sendSession(uint32_t, uint64_t, std::shared_ptr<const std::string>) override;
    void closeSession(uint32_t, uint64_t) override;
    void sendStats(JsonObject) override;
    static esp_err_t wsPreHandshake(httpd_req_t*);
    static esp_err_t wsPostHandshake(httpd_req_t*);
    static void prepareWsHandler(httpd_uri_t&);
    static esp_err_t wsHandlerCustom(httpd_req_t*) { return ESP_OK; }
    esp_err_t incomingWs(httpd_req_t*);
    esp_err_t authenticate(httpd_req_t*, bool& granted) { granted = allow; return ESP_OK; }
    bool allow = true;
    JsonDocument stats() { JsonDocument result; sendStats(result.to<JsonObject>()); return result; }
   private:
    httpd_handle_t _server = reinterpret_cast<void*>(1);
    std::mutex _wsSessionMutex, _sendMutex;
    std::map<int, uint64_t> _controlSessions;
    size_t _queuedBytes = 0, _queuedWorks = 0;
    uint64_t _sendRejected = 0, _sendFailed = 0, _slowClosed = 0;
  };
}
#include "httpd.inc"

using esp32m::ui::Httpd;
auto body(const char* value = "payload") { return std::make_shared<const std::string>(value); }
constexpr const char* RequestText = R"({"type":"request","target":"sensor","name":"state-get","seq":1})";

void requestLifetimes() {
  fake::reset(); esp32m::Ui ui; Httpd http(ui);
  const auto old = http.open(7);
  unsigned dispatched = 0;
  std::unique_ptr<esp32m::Response> delayed;
  fake::dispatch = [&](auto& request) { ++dispatched; delayed.reset(request.makeResponse()); request.respond(); };
  http.receive(7, RequestText, old); ui.process(http);
  assert(dispatched == 1 && delayed);
  assert(ui._wsStateReads == 1 && ui._wsDeviceReads == 0);
  auto* response = static_cast<esp32m::ui::Rb*>(delayed.get());
  assert(response->clientId == 7 && response->generation == old);
  http.physicalClose(7); const auto next = http.open(7);
  assert(next != old);
  http.receive(7, RequestText, old); ui.process(http);
  assert(dispatched == 1);
  assert(ui._wsStateReads == 1);
  fake::drain(); assert(fake::frames.empty()); // queued old reply cannot reach successor
  http.receive(7, RequestText, next); http.physicalClose(7); http.open(7); ui.process(http);
  assert(dispatched == 1); // accepted request of closed predecessor is discarded
  fake::dispatch = [&](auto&) { ++dispatched; http.physicalClose(7); http.open(7); };
  std::string batch = "[" + std::string(RequestText) + "," + RequestText + ']';
  http.receive(7, batch.c_str(), http.sessionGeneration(7)); ui.process(http);
  assert(dispatched == 2); // second request in old batch is checked again
  assert(ui._wsStateReads == 2); // rejected predecessor requests do not count
  fake::drain();
  std::cout << "PASS inbound generations, queued/deferred reply identity, FD reuse and batched dispatch\n";
}

void queueAndSendFailures() {
  fake::reset(); esp32m::Ui ui; Httpd http(ui); auto generation = http.open(3);
  auto shared = body(); std::weak_ptr<const std::string> retained = shared;
  assert(http.sendSession(3, generation, shared) == ESP_OK); shared.reset();
  assert(!retained.expired() && http.stats()["queuedWorks"] == 1);
  fake::drain(); assert(retained.expired() && fake::frames.size() == 1);
  assert(http.stats()["queuedBytes"] == 0 && http.stats()["queuedWorks"] == 0);
  fake::sendFails = true;
  assert(http.sendSession(3, generation, body()) == ESP_OK); fake::drain();
  assert(http.sessionGeneration(3) == 0 && fake::shutdowns == std::vector<int>{3});
  assert(http.stats()["failed"] == 1 && http.stats()["queuedWorks"] == 0);
  std::cout << "PASS shared payload lifetime, async failure, physical shutdown and charge release\n";
}

void closeAndQueuePressure() {
  fake::reset(); esp32m::Ui ui; Httpd http(ui); auto old = http.open(4);
  http.closeSession(4, old); http.physicalClose(4); const auto next = http.open(4);
  fake::drain(); assert(fake::shutdowns.empty() && http.sessionAlive(4, next));
  http.closeSession(4, old); assert(fake::queue.empty());
  fake::capacity = 0; // send and close work admission both fail
  assert(http.sendSession(4, next, body()) == ESP_FAIL);
  assert(fake::shutdowns == std::vector<int>{4} && http.sessionGeneration(4) == 0);
  assert(http.stats()["queuedBytes"] == 0 && http.stats()["queuedWorks"] == 0);
  fake::capacity = 1000; old = http.open(8);
  for (unsigned i = 0; i < 32; i++) assert(http.sendSession(8, old, body()) == ESP_OK);
  assert(http.sendSession(8, old, body()) == ESP_ERR_NO_MEM);
  assert(http.stats()["queuedWorks"] == 32 && http.stats()["queuedBytes"].as<unsigned>() <= 32768);
  fake::drain(); assert(http.stats()["queuedWorks"] == 0 && http.stats()["queuedBytes"] == 0);
  old = http.open(9);
  auto huge = std::make_shared<const std::string>(33000, 'x');
  assert(http.sendSession(9, old, huge) == ESP_ERR_NO_MEM); fake::drain();
  assert(http.stats()["queuedWorks"] == 0);
  std::cout << "PASS generation-safe close, full control queue fallback, bounded works/bytes and rollback\n";
}

void clientQueueIdentity() {
  fake::reset(); esp32m::Ui ui; Httpd http(ui);
  esp32m::ui::Client client(&http, 1, 1);
  for (unsigned i = 0; i < 11; i++) {
    auto doc = new JsonDocument(); (*doc)["name"] = i % 2 ? "state-subscribe" : "state-unsubscribe";
    (*doc)["seq"] = i; assert(client.enqueue(doc));
  }
  auto rejected = new JsonDocument(); (*rejected)["name"] = "state-resync";
  assert(!client.enqueue(rejected)); delete rejected;
  client.disconnected(); rejected = new JsonDocument(); assert(!client.enqueue(rejected)); delete rejected;
  std::cout << "PASS distinct lifecycle requests, finite incoming queue and disconnected enqueue rejection\n";
}

void batchedStartupBurst() {
  fake::reset(); esp32m::Ui ui; Httpd http(ui); auto generation = http.open(10);
  std::vector<int> dispatched;
  fake::dispatch = [&](auto& request) {
    dispatched.push_back(request.seq());
    request.respond();
  };
  // The frontend sends arrays even for a single request. Queue the startup
  // burst before giving the UI task a turn, as several mounting cards do.
  for (int seq = 1; seq <= 8; ++seq) {
    auto text = "[{\"type\":\"request\",\"target\":\"sensor\","
                "\"name\":\"state-get\",\"seq\":" + std::to_string(seq) + "}]";
    http.receive(10, text.c_str(), generation);
  }
  ui.process(http); fake::drain();
  assert(dispatched == (std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));
  assert(fake::frames.size() == 8 && ui._wsStateReads == 8);
  for (size_t i = 0; i < fake::frames.size(); ++i) {
    JsonDocument reply; assert(!deserializeJson(reply, fake::frames[i].second));
    assert(reply["seq"] == int(i + 1) && reply["source"] == "sensor");
    assert(reply["name"] == "state-get" && reply["error"].isUnbound());
  }
  esp32m::ui::Client client(&http, 11, 1);
  auto makeBatch = [](int seq) {
    auto doc = new JsonDocument();
    auto request = doc->to<JsonArray>().add<JsonObject>();
    request["type"] = "request"; request["target"] = "sensor";
    request["name"] = "state-get"; request["seq"] = seq;
    return doc;
  };
  for (int seq = 1; seq <= 5; ++seq) assert(client.enqueue(makeBatch(seq)));
  auto duplicate = makeBatch(3); assert(!client.enqueue(duplicate)); delete duplicate;
  assert(client.enqueue(makeBatch(6)));
  // An object and a singleton array are different envelopes, despite carrying
  // the same request; neither may collapse to a pair of null object casts.
  auto object = new JsonDocument();
  assert(!deserializeJson(*object, RequestText)); assert(client.enqueue(object));
  std::cout << "PASS singleton-array startup burst, exact batch identity and unlike envelopes\n";
}

void rejectedBatchReplies() {
  fake::reset(); esp32m::Ui ui; Httpd http(ui); auto generation = http.open(12);
  esp32m::ui::_errors.clear(); esp32m::ui::_errors.add("busy");
  unsigned dispatched = 0, writes = 0;
  fake::dispatch = [&](auto& request) {
    ++dispatched;
    if (!strcmp(request.name(), "state-set") || !strcmp(request.name(), "set-total")) ++writes;
    request.respond();
  };
  auto fill = [&] {
    for (int seq = 100; seq < 111; ++seq) {
      auto text = "[{\"type\":\"request\",\"target\":\"sensor\","
                  "\"name\":\"state-get\",\"seq\":" + std::to_string(seq) + "}]";
      http.receive(12, text.c_str(), generation);
    }
  };
  const char* rejected = R"([{"type":"request","target":"relay","name":"state-set","seq":201,"data":{"on":true}},{"type":"request","target":"SM10","name":"set-total","seq":202,"data":{"total":"0.2","confirm":true}},{"type":"request","target":"ui","name":"state-subscribe","seq":203,"data":{"id":"late","target":"SM10","view":"state"}}])";
  fill(); http.receive(12, rejected, generation);
  ui.process(http); fake::drain();
  assert(dispatched == 11 && writes == 0 && fake::frames.size() == 14);
  unsigned busy = 0;
  std::map<int, unsigned> responses;
  for (auto& frame : fake::frames) {
    JsonDocument reply; assert(!deserializeJson(reply, frame.second));
    const int seq = reply["seq"]; ++responses[seq];
    assert(frame.first == 12);
    if (seq >= 201 && seq <= 203) {
      ++busy; assert(reply["error"] == "busy");
      assert(reply["source"] == (seq == 201 ? "relay" : seq == 202 ? "SM10" : "ui"));
      assert(reply["name"] == (seq == 201 ? "state-set" : seq == 202 ? "set-total" : "state-subscribe"));
    } else {
      assert(seq >= 100 && seq < 111 && reply["error"].isUnbound());
    }
  }
  assert(busy == 3 && responses.size() == 14);
  for (auto& response : responses) assert(response.second == 1);
  // Queued busy responses retain the rejected batch's generation; closing and
  // reusing that FD cannot deliver them or replay queued writes to its successor.
  fake::frames.clear(); fill(); http.receive(12, rejected, generation);
  http.physicalClose(12); const auto successor = http.open(12);
  assert(successor != generation);
  ui.process(http); fake::drain();
  assert(fake::frames.empty() && dispatched == 11 && writes == 0);
  std::cout << "PASS overflow replies correlate every batch request without writes, replay or FD leakage\n";
}

void inboundReadCounters() {
  fake::reset(); esp32m::Ui ui; Httpd http(ui); auto generation = http.open(5);
  fake::dispatch = [](auto& request) { request.respond(); };
  http.receive(5, RequestText, generation);
  http.receive(5, R"({"type":"request","target":"sensor","name":"device-state","seq":2})", generation);
  http.receive(5, R"({"type":"request","target":"sensor","name":"config-get","seq":3})", generation);
  ui.process(http);
  assert(ui._wsStateReads == 1 && ui._wsDeviceReads == 1);
  // Internal observation publishes the ordinary Request event directly. It
  // never traverses incoming Req dispatch, even though its origin is UI.
  class Capture : public esp32m::Request {
   public:
    Capture() : Request("state-get", 0, "sensor", JsonVariantConst{}, "ui") {}
   protected:
    void respondImpl(const char*, JsonVariantConst, bool) override {}
    esp32m::Response* makeResponseImpl() override { return nullptr; }
  } capture;
  capture.publish();
  assert(ui._wsStateReads == 1 && ui._wsDeviceReads == 1);
  http.receive(5, R"({"type":"request","target":"ui","name":"state-subscriptions-stats","seq":4})", generation);
  ui.process(http); fake::drain();
  JsonDocument response; assert(!deserializeJson(response, fake::frames.back().second));
  assert(response["data"]["wsStateReads"] == 1 && response["data"]["wsDeviceReads"] == 1);
  assert(response["data"]["taskStackBytes"] == 8192);
  assert(response["data"]["taskStackMinFreeBytes"] == 3072);
  std::cout << "PASS WS read counters exclude internal captures and appear in stats\n";
}

void idf61Handshake() {
  fake::reset(); esp32m::Ui ui; Httpd http(ui);
  httpd_uri_t endpoint;
  endpoint.handler = [](httpd_req_t* request) {
    return static_cast<Httpd*>(request->user_ctx)->incomingWs(request);
  };
  Httpd::prepareWsHandler(endpoint);
  assert(endpoint.ws_pre_handshake_cb && endpoint.ws_post_handshake_cb);
  httpd_req_t request; request.fd = 6; request.user_ctx = &http;
  http.allow = false;
  assert(endpoint.ws_pre_handshake_cb(&request) == ESP_FAIL);
  assert(http.sessionGeneration(6) == 0); // no upgrade/session on rejection
  http.allow = true;
  assert(endpoint.ws_pre_handshake_cb(&request) == ESP_OK);
  assert(http.sessionGeneration(6) == 0);
  // IDF6.1 sends101, then POST, and deliberately never invokes the normal
  // URI handler for the HTTP GET upgrade. The first text frame must still
  // find an established accepted session generation.
  assert(endpoint.ws_post_handshake_cb(&request) == ESP_OK);
  auto generation = http.sessionGeneration(6); assert(generation != 0);
  fake::dispatch = [](auto& read) { read.respond(); };
  request.method = 0; request.payload = RequestText;
  assert(endpoint.handler(&request) == ESP_OK);
  ui.process(http); fake::drain();
  assert(ui._wsStateReads == 1 && fake::frames.size() == 1);
  assert(http.sessionAlive(6, generation));
  endpoint.handler = Httpd::wsHandlerCustom;
  Httpd::prepareWsHandler(endpoint);
  assert(endpoint.ws_pre_handshake_cb && !endpoint.ws_post_handshake_cb);
  std::cout << "PASS ESP-IDF6.1 PRE/POST handshake, rejected upgrade and first text-frame response\n";
}

int main() { requestLifetimes(); queueAndSendFailures(); closeAndQueuePressure(); clientQueueIdentity(); batchedStartupBurst(); rejectedBatchReplies(); inboundReadCounters(); idf61Handshake(); }
