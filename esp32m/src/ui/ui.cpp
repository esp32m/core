#include <esp_task_wdt.h>

#include "esp32m/app.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/json.hpp"
#include "esp32m/ui.hpp"
#include "esp32m/ui/transport.hpp"

namespace esp32m {
  namespace ui {
    JsonDocument /*<JSON_ARRAY_SIZE(1)>*/ _errors;

    std::string makeResponse(const char* name, const char* source, int seq,
                             JsonVariantConst data, bool error, bool partial) {
      /*size_t mu = data.memoryUsage();*/
      JsonDocument doc /*(mu + JSON_OBJECT_SIZE(6))*/;
      auto msg = doc.to<JsonObject>();
      msg["type"] = "response";
      if (name)
        msg["name"] = name;
      if (source)
        msg["source"] = source;
      if (partial)
        msg["partial"] = true;
      if (seq)
        msg["seq"] = seq;
      msg[error ? "error" : "data"] = data;
      std::string result;
      serializeJson(doc, result);
      return result;
      // return json::allocSerialize(doc);
    }

    class Rb : public Response {
     public:
      uint32_t clientId;
      Rb(const char* transport, const char* name, const char* source, int seq,
         uint32_t cid)
          : Response(transport, name, source, seq), clientId(cid) {}
    };

    class Req : public Request {
     public:
      static void process(Transport* transport, uint32_t cid,
                          JsonVariantConst msg) {
        if (msg.isNull())
          return;
        if (msg["type"] != Type)
          return;
        const char* name = msg["name"];
        if (name) {
          /*if (!strcmp(name, "config-get"))
            json::dump(ui, msg, "process request");*/
          Req ev(transport, name, msg["seq"], msg["target"], msg["data"], cid);
          ev.publish();
        }
      }

     protected:
      void respondImpl(const char* source, const JsonVariantConst data,
                       bool error) override {
        std::string text =
            ui::makeResponse(name(), source, seq(), data, error, false);
        _transport->sendTo(_clientId, text.c_str());
        // free(text);
      }

      Response* makeResponseImpl() override {
        return new Rb(_transport->name(), name(), target(), seq(), _clientId);
      }

     private:
      Transport* _transport;
      uint32_t _clientId;
      Req(Transport* transport, const char* name, int seq, const char* target,
          const JsonVariantConst data, uint32_t clientId)
          : Request(name, seq, target, data, "ui"),
            _transport(transport),
            _clientId(clientId) {}
    };

    Client::Client(Transport* transport, uint32_t id) {
      _name = string_printf("%s-%u", transport->name(), id);
    }

    void Transport::refreshClientIdsViewLocked() {
      std::vector<uint32_t> ids;
      ids.reserve(_clients.size());
      for (auto& [cid, client] : _clients)
        if (!client->isDisconnected())
          ids.push_back(cid);
      _clientIdsView.store(std::make_shared<const std::vector<uint32_t>>(
                               std::move(ids)),
                           std::memory_order_release);
    }

    void Transport::process() {
      for (;;) {
        JsonDocument* req = nullptr;
        uint32_t cid = 0;
        bool refreshSnapshot = false;
        {
          std::lock_guard<std::mutex> guard(_clientsMutex);
          for (auto it = _clients.begin(); it != _clients.end();) {
            auto client = it->second.get();
            if (client->isDisconnected()) {
              it = _clients.erase(it);
              refreshSnapshot = true;
            } else {
              req = client->dequeue();
              cid = it->first;
              if (req)
                break;
              ++it;
            }
          }
          if (refreshSnapshot)
            refreshClientIdsViewLocked();
        }
        if (!req)
          break;
        JsonArrayConst arr = req->as<JsonArrayConst>();
        if (!arr.isNull())
          for (JsonVariantConst v : arr) {
            esp_task_wdt_reset();
            ui::Req::process(this, cid, v.as<JsonObjectConst>());
          }
        else
          ui::Req::process(this, cid, req->as<JsonObjectConst>());
        delete req;
      }
    }

    void Transport::sessionClosed(uint32_t cid) {
      std::lock_guard<std::mutex> guard(_clientsMutex);
      auto i = _clients.find(cid);
      if (i != _clients.end()) {
        i->second->disconnected();
        LOGD(i->second, "session closed");
        refreshClientIdsViewLocked();
      }
    }

    void Transport::incoming(uint32_t cid, void* data, size_t len) {
      if (!len || !data)
        return;
      /*std::string str((const char *)data, len);
      logd("%d: incoming %s", cid, str.c_str());*/
      JsonDocument* dp = json::parse((const char*)data, len);
      this->incoming(cid, dp);
    }
    void Transport::incoming(uint32_t cid, JsonDocument* json) {
      if (!json || json->isNull())
        return;
      ui::Client* c;
      {
        std::lock_guard<std::mutex> guard(_clientsMutex);
        auto i = _clients.find(cid);
        if (i == _clients.end()) {
          c = new ui::Client(this, cid);
          _clients[cid] = std::unique_ptr<ui::Client>(c);
          LOGD(c, "session opened");
          refreshClientIdsViewLocked();
        } else
          c = i->second.get();
      }
      if (!c->enqueue(json)) {
        JsonObjectConst req = json->as<JsonObjectConst>();
        int seq = req["seq"];
        const char* type = req["type"];
        if (seq && type) {
          std::string text = ui::makeResponse(req["name"], type, seq,
                                              ui::_errors[0], true, false);
          sendTo(cid, text.c_str());
        }
        delete json;
      } else
        _ui->notifyIncoming();
    }

  }  // namespace ui

  void Ui::handleEvent(Event& ev) {
    if (EventInit::is(ev, 0)) {
      if (!ui::_errors.size())
        ui::_errors.add("busy");
      auto transports = _transportsView.load(std::memory_order_acquire);
      for (auto* transport : *transports)
        transport->init(this);
      xTaskCreate([](void* self) { ((Ui*)self)->run(); }, "m/ui", 1024 * 6,
                  this, tskIDLE_PRIORITY, &_task);
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
      std::string text;
      serializeJson(msg, text);
      broadcast(text.c_str());
      return;
    }
    Response* r;
    auto transports = _transportsView.load(std::memory_order_acquire);
    for (auto* transport : *transports)
      if (Response::is(ev, transport->name(), &r)) {
        ui::Rb* resp = (ui::Rb*)r;
        // logI("resp: [%s] [%s] [%d]", r->name(), r->source(),  r->seq());
        JsonDocument* doc = r->data();
        JsonVariantConst data =
            doc ? doc->as<JsonVariantConst>() : json::null<JsonVariantConst>();
        std::string response =
            ui::makeResponse(r->name(), r->source(), r->seq(), data,
                             r->isError(), r->isPartial());
        transport->sendTo(resp->clientId, response.c_str());
        // free(response);
      }
  }

  void Ui::broadcast(const char* text) {
    auto transports = _transportsView.load(std::memory_order_acquire);
    for (auto* transport : *transports)
      transport->broadcast(text);
  }

  void Ui::run() {
    esp_task_wdt_add(NULL);
    for (;;) {
      esp_task_wdt_reset();
      auto transports = _transportsView.load(std::memory_order_acquire);
      for (auto* transport : *transports)
        transport->process();
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
  }

}  // namespace esp32m