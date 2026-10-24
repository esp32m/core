#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <algorithm>

#include "esp32m/app.hpp"
#include "esp32m/device.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/json.hpp"
#include "esp32m/ui.hpp"
#include "esp32m/ui/transport.hpp"

namespace esp32m {
  namespace ui {
    JsonDocument /*<JSON_ARRAY_SIZE(1)>*/ _errors;

    Payload makeResponse(const char* name, const char* source, int seq,
                             JsonVariantConst data, bool error, bool partial) {
      /*size_t mu = data.memoryUsage();*/
      JsonDocument doc /*(mu + JSON_OBJECT_SIZE(6))*/;
      auto msg = doc.to<JsonObject>();
      msg["type"] = "response";
      msg["serverTimeMs"] = esp_timer_get_time() / 1000ULL;
      if (name)
        msg["name"] = name;
      if (source)
        msg["source"] = source;
      if (partial)
        msg["partial"] = true;
      if (seq)
        msg["seq"] = seq;
      msg[error ? "error" : "data"] = data;
      if (doc.overflowed()) return {};
      return Payload::fromJson(doc);
    }

    class Rb : public Response {
     public:
      uint32_t clientId;
      uint64_t generation;
      Rb(const char* transport, const Request& request, const char* source,
         uint32_t cid, uint64_t generation)
          : Response(transport, request, source), clientId(cid),
            generation(generation) {}
    };

    class Req : public Request {
     public:
      static void process(Transport* transport, uint32_t cid, uint64_t generation,
                          JsonVariantConst msg) {
        if (!transport->sessionAlive(cid, generation))
          return;
        if (msg.isNull())
          return;
        if (msg["type"] != Type)
          return;
        const char* name = msg["name"];
        if (name) {
          // Count actual accepted WS read requests, not the coordinator's
          // external-origin CaptureRequest events used for shared observation.
          auto* ui = transport->_ui;
          if (!strcmp(transport->name(), "ui-http")) {
            if (!strcmp(name, "state-get"))
              ++ui->_wsStateReads;
            else if (!strcmp(name, "device-state"))
              ++ui->_wsDeviceReads;
          }
          /*if (!strcmp(name, "config-get"))
            json::dump(ui, msg, "process request");*/
          Req ev(transport, name, msg["seq"], msg["target"], msg["data"], cid,
                 generation);
          const char* target = msg["target"];
          if (target && !strcmp(target, "ui")) {
            if (ui->_subscriptions.request({transport, cid, generation}, name,
                                            msg["seq"], msg["data"], millis()))
              return;
            if (!strcmp(name, "state-subscriptions-stats")) {
              JsonDocument stats;
              ui->_subscriptions.stats(stats.to<JsonObject>());
              stats["wsStateReads"] = ui->_wsStateReads;
              stats["wsDeviceReads"] = ui->_wsDeviceReads;
              stats["taskStackBytes"] = CONFIG_ESP32M_UI_TASK_STACK_SIZE;
              // ESP-IDF's Xtensa StackType_t is a byte, so this is bytes too.
              stats["taskStackMinFreeBytes"] = uxTaskGetStackHighWaterMark(nullptr);
              transport->sendStats(stats["output"].to<JsonObject>());
              ev.respond("ui", stats, false);
              return;
            }
            if (!strcmp(name, "ping")) {
              JsonDocument clock;
              clock["serverTimeMs"] = esp_timer_get_time() / 1000ULL;
              ev.respond("ui", clock.as<JsonVariantConst>(), false);
              return;
            }
          }
          ev.publish();
        }
      }

     protected:
      void respondImpl(const char* source, const JsonVariantConst data,
                       bool error) override {
        auto text =
            ui::makeResponse(name(), source, seq(), data, error, false);
        if (_transport->sendSession(_clientId, _generation, std::move(text)) != ESP_OK)
          _transport->closeSession(_clientId, _generation);
      }

      Response* makeResponseImpl() override {
        return new Rb(_transport->name(), *this, target(), _clientId, _generation);
      }

     private:
      Transport* _transport;
      uint32_t _clientId;
      uint64_t _generation;
      Req(Transport* transport, const char* name, int seq, const char* target,
          const JsonVariantConst data, uint32_t clientId, uint64_t generation)
          : Request(name, seq, target, data, "ui"),
            _transport(transport),
            _clientId(clientId), _generation(generation) {}
    };

    class CaptureRequest : public Request {
     public:
      CaptureRequest(const std::string& target, const std::string& view,
                     JsonVariantConst query, JsonDocument& result)
          : Request(view == "device" ? "device-state" : "state-get", 0,
                    target.c_str(), query, "ui"), _result(result) {}
      bool captured = false;
      const char* failure = "unsupported-state";
     protected:
      void respondImpl(const char*, JsonVariantConst data, bool error) override {
        if (error)
          failure = data == "unhandled" ? "unsupported-state" : "snapshot-error";
        else if (measureJson(data) > StateSubscriptions::MaxSnapshotBytes)
          failure = "snapshot-limit";
        else {
          _result.set(data);
          captured = true;
        }
      }
      Response* makeResponseImpl() override {
        // Deferred/custom state requires a dedicated adapter; this response
        // transport is deliberately not routed to a UI client.
        return new Response("ui-capture", *this, target());
      }
     private:
      JsonDocument& _result;
    };

    Client::Client(Transport* transport, uint32_t id, uint64_t generation)
        : _generation(generation) {
      _name = string_printf("%s-%u", transport->name(), id);
    }

    void Transport::refreshClientIdsViewLocked() {
      std::vector<uint32_t> ids;
      ids.reserve(_clients.size());
      for (auto& [cid, client] : _clients)
        if (!client->isDisconnected())
          ids.push_back(cid);
      _clientIdsView.store(
          std::make_shared<const std::vector<uint32_t> >(std::move(ids)),
          std::memory_order_release);
    }

    void Transport::process() {
      for (;;) {
        JsonDocument* req = nullptr;
        uint32_t cid = 0;
        uint64_t generation = 0;
        {
          std::lock_guard<std::mutex> guard(_clientsMutex);
          for (auto it = _clients.begin(); it != _clients.end();) {
            auto client = it->second.get();
            if (client->isDisconnected()) {
              it = _clients.erase(it);
            } else {
              req = client->dequeue();
              cid = it->first;
              generation = client->generation();
              if (req)
                break;
              ++it;
            }
          }
          // Removal never allocates a new client-ID view. A later normal
          // session open rebuilds it; broadcast filters stale disconnected IDs.
        }
        if (!req)
          break;
        JsonArrayConst arr = req->as<JsonArrayConst>();
        if (!arr.isNull())
          for (JsonVariantConst v : arr) {
            esp_task_wdt_reset();
            ui::Req::process(this, cid, generation, v.as<JsonObjectConst>());
          }
        else
          ui::Req::process(this, cid, generation, req->as<JsonObjectConst>());
        delete req;
      }
    }

    void Transport::sessionClosed(uint32_t cid) {
      std::lock_guard<std::mutex> guard(_clientsMutex);
      auto i = _clients.find(cid);
      if (i != _clients.end()) {
        i->second->disconnected();
        LOGD(i->second, "session closed");
        // Keep the immutable view until a normal open rebuilds it. Broadcast
        // filters this disconnected generation without allocating on close.
      }
      if (_ui)
        _ui->notifyIncoming();
    }

    uint64_t Transport::sessionOpened(uint32_t cid) {
      std::lock_guard<std::mutex> guard(_clientsMutex);
      uint64_t generation = ++_nextGeneration;
      _clients[cid] = std::make_unique<Client>(this, cid, generation);
      refreshClientIdsViewLocked();
      if (_ui)
        _ui->notifyIncoming();
      return generation;
    }

    bool Transport::sessionAlive(uint32_t cid, uint64_t generation) {
      std::lock_guard<std::mutex> guard(_clientsMutex);
      auto i = _clients.find(cid);
      return i != _clients.end() && !i->second->isDisconnected() &&
             i->second->generation() == generation;
    }

    uint64_t Transport::sessionGeneration(uint32_t cid) {
      std::lock_guard<std::mutex> guard(_clientsMutex);
      auto i = _clients.find(cid);
      return i == _clients.end() || i->second->isDisconnected()
                 ? 0 : i->second->generation();
    }

    void Transport::closeSession(uint32_t cid, uint64_t generation) {
      std::lock_guard<std::mutex> guard(_clientsMutex);
      auto i = _clients.find(cid);
      if (i != _clients.end() && i->second->generation() == generation) {
        i->second->disconnected();
        // Closing on memory pressure must not allocate a replacement view.
        // Broadcast filters stale IDs; a later normal open rebuilds the view.
        if (_ui)
          _ui->notifyIncoming();
      }
    }

    void Transport::incoming(uint32_t cid, void* data, size_t len,
                              uint64_t generation) {
      if (!len || !data)
        return;
      /*std::string str((const char *)data, len);
      logd("%d: incoming %s", cid, str.c_str());*/
      JsonDocument* dp = json::parse((const char*)data, len);
      this->incoming(cid, dp, generation);
    }
    void Transport::incoming(uint32_t cid, JsonDocument* json,
                              uint64_t expectedGeneration) {
      if (!json)
        return;
      if (json->isNull()) {
        delete json;
        return;
      }
      bool queued = false;
      uint64_t generation;
      {
        std::lock_guard<std::mutex> guard(_clientsMutex);
        auto i = _clients.find(cid);
        if (expectedGeneration &&
            (i == _clients.end() || i->second->isDisconnected() ||
             i->second->generation() != expectedGeneration)) {
          delete json;
          return;
        }
        if (i == _clients.end()) {
          // Non-WS transports open lazily. HTTPD opens at handshake instead.
          auto c = std::make_unique<Client>(this, cid, ++_nextGeneration);
          i = _clients.emplace(cid, std::move(c)).first;
          refreshClientIdsViewLocked();
        }
        generation = i->second->generation();
        queued = i->second->enqueue(json);
      }
      if (!queued) {
        auto reject = [&](JsonObjectConst req) {
          int seq = req["seq"];
          const char* type = req["type"];
          if (seq && type && !strcmp(type, "request")) {
            const char* source = req["target"] | "ui";
            auto text = ui::makeResponse(req["name"], source, seq,
                                                ui::_errors[0], true, false);
            if (sendSession(cid, generation, std::move(text)) != ESP_OK)
              closeSession(cid, generation);
          }
        };
        auto batch = json->as<JsonArrayConst>();
        if (!batch.isNull())
          for (JsonVariantConst request : batch)
            reject(request.as<JsonObjectConst>());
        else
          reject(json->as<JsonObjectConst>());
        delete json;
      } else
        _ui->notifyIncoming();
    }

  }  // namespace ui

  Ui::Ui()
      : _subscriptions(
            [](const std::string& target, const std::string& view,
               JsonVariantConst query, JsonDocument& result) -> const char* {
              ui::CaptureRequest capture(target, view, query, result);
              capture.publish();
              return capture.captured ? nullptr : capture.failure;
            },
            [](const ui::StateSubscriptions::Session& session,
               ui::StateSubscriptions::Payload payload) {
              if (session.transport->sendSession(session.cid, session.generation,
                                                  std::move(payload)) == ESP_OK)
                return true;
              session.transport->closeSession(session.cid, session.generation);
              return false;
            },
            [](const ui::StateSubscriptions::Session& session) {
              return session.transport->sessionAlive(session.cid,
                                                     session.generation);
            },
            [](const ui::StateSubscriptions::Session& session) {
              session.transport->closeSession(session.cid, session.generation);
            },
            []() -> uint64_t { return esp_timer_get_time() / 1000ULL; }) {
    _transportsView = std::make_shared<const std::vector<ui::Transport*>>();
  }

  void Ui::handleEvent(Event& ev) {
    EventStateChanged* stateChanged;
    dev::ComponentStateChanged* componentChanged;
    const AppObject* source = nullptr;
    if (EventStateChanged::is(ev, &stateChanged))
      source = stateChanged->object();
    else if (dev::ComponentStateChanged::is(ev, &componentChanged) &&
             componentChanged->component())
      source = componentChanged->component()->device();
    if (source && _subscriptions.invalidate(source->interactiveName(), millis())) {
      notifyIncoming();
      return;
    }
    if (EventInit::is(ev, 0)) {
      if (!ui::_errors.size())
        ui::_errors.add("busy");
      xTaskCreate([](void* self) { ((Ui*)self)->run(); }, "m/ui",
                  CONFIG_ESP32M_UI_TASK_STACK_SIZE, this, tskIDLE_PRIORITY,
                  &_task);
      return;
    }
    Broadcast* b;
    if (Broadcast::is(ev, &b)) {
      auto data = b->data();
      JsonDocument msg;
      msg["type"] = b->type();
      msg["source"] = b->source();
      msg["name"] = b->name();
      if (data)
        msg["data"] = data;
      auto payload = msg.overflowed() ? ui::Payload{} : ui::Payload::fromJson(msg);
      if (payload) broadcast(payload.c_str());
      return;
    }
    Response* r;
    auto transports = transportsView();
    for (auto* transport : *transports)
      if (Response::is(ev, transport->name(), &r)) {
        ui::Rb* resp = (ui::Rb*)r;
        // logI("resp: [%s] [%s] [%d]", r->name(), r->source(),  r->seq());
        JsonDocument* doc = r->data();
        JsonVariantConst data =
            doc ? doc->as<JsonVariantConst>() : json::null<JsonVariantConst>();
        auto response =
            ui::makeResponse(r->name(), r->source(), r->seq(), data,
                             r->isError(), r->isPartial());
        if (transport->sendSession(resp->clientId, resp->generation,
                                   std::move(response)) != ESP_OK)
          transport->closeSession(resp->clientId, resp->generation);
      }
  }

  void Ui::broadcast(const char* text) {
    auto transports = transportsView();
    for (auto* transport : *transports) transport->broadcast(text);
  }

  void Ui::run() {
    esp_task_wdt_add(NULL);
    auto transports = transportsView();
    for (auto* transport : *transports) transport->init(this);
    for (;;) {
      esp_task_wdt_reset();
      transports = transportsView();
      for (auto* transport : *transports) transport->process();
      _subscriptions.process(millis());
      auto wait = _subscriptions.waitTime(millis());
      ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, pdMS_TO_TICKS(wait)));
    }
  }

}  // namespace esp32m
