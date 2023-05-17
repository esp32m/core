#include <esp_task_wdt.h>

#include "esp32m/app.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/json.hpp"
#include "esp32m/ui.hpp"

namespace esp32m {
  namespace ui {
    StaticJsonDocument<JSON_ARRAY_SIZE(1)> _errors;

    char *makeResponse(const char *name, const char *source, int seq,
                       JsonVariantConst data, bool error, bool partial) {
      size_t mu = data.memoryUsage();
      DynamicJsonDocument msg(mu + JSON_OBJECT_SIZE(6));
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
      return json::allocSerialize(msg);
    }

    class Rb : public Response {
     public:
      uint32_t clientId;
      Rb(const char *transport, const char *name, const char *source, int seq,
         uint32_t cid)
          : Response(transport, name, source, seq), clientId(cid) {}
    };

    class Req : public Request {
     public:
      static void process(Ui *ui, uint32_t cid, JsonVariantConst msg) {
        if (msg.isNull())
          return;
        if (msg["type"] != NAME)
          return;
        const char *name = msg["name"];
        if (name) {
          /*if (!strcmp(name, "config-get"))
            json::dump(ui, msg, "process request");*/
          Req ev(ui, name, msg["seq"], msg["target"], msg["data"], cid);
          ev.publish();
        }
      }

     protected:
      void respondImpl(const char *source, const JsonVariantConst data,
                       bool error) override {
        char *text =
            ui::makeResponse(name(), source, seq(), data, error, false);
        _ui->wsSend(_clientId, text);
        free(text);
      }

      Response *makeResponseImpl() override {
        return new Rb(_ui->_transport->name(), name(), target(), seq(),
                      _clientId);
      }

     private:
      Ui *_ui;
      uint32_t _clientId;
      Req(Ui *ui, const char *name, int seq, const char *target,
          const JsonVariantConst data, uint32_t clientId)
          : Request(name, seq, target, data, "ui"),
            _ui(ui),
            _clientId(clientId) {}
    };

    Client::Client(Ui *ui, uint32_t id) {
      _name = string_printf("%s-%u", ui->transport()->name(), id);
    }

  }  // namespace ui

  Ui::Ui(ui::Transport *transport) : _transport(transport) {}
  Ui::~Ui() {
    delete (_transport);
  }
  void Ui::addAsset(const char *uri, const char *contentType,
                    const uint8_t *start, const uint8_t *end,
                    const char *contentEncoding, const char *etag) {
    _assets.emplace_back(uri, contentType, start, end, contentEncoding, etag);
  }

  /*    bool Ui::handleRequest(Request &req)
  {
      if (AppObject::handleRequest(req))
          return true;
      return false;
  }*/

  void Ui::handleEvent(Event &ev) {
    if (EventInit::is(ev, 0)) {
      if (!ui::_errors.size())
        ui::_errors.add("busy");
      _transport->init(this);
      xTaskCreate([](void *self) { ((Ui *)self)->run(); }, "m/ui", 4096, this,
                  tskIDLE_PRIORITY, &_task);
      return;
    }
    Broadcast *b;
    if (Broadcast::is(ev, &b)) {
      auto data = b->data();
      size_t mu = data.memoryUsage();
      DynamicJsonDocument msg(mu + JSON_OBJECT_SIZE(4));
      msg["type"] = b->type();
      msg["source"] = b->source();
      msg["name"] = b->name();
      if (data)
        msg["data"] = data;
      char *text = json::allocSerialize(msg);
      wsSend(text);
      free(text);
      return;
    }
    Response *r;
    if (Response::is(ev, _transport->name(), &r)) {
      ui::Rb *resp = (ui::Rb *)r;
      // logI("resp: [%s] [%s] [%d]", r->name(), r->source(),  r->seq());
      DynamicJsonDocument *doc = r->data();
      JsonVariantConst data =
          doc ? doc->as<JsonVariantConst>() : json::null<JsonVariantConst>();
      char *response = ui::makeResponse(r->name(), r->source(), r->seq(), data,
                                        r->isError(), r->isPartial());
      wsSend(resp->clientId, response);
      free(response);
    }
  }

  void Ui::sessionClosed(uint32_t cid) {
    std::lock_guard<std::mutex> guard(_mutex);
    auto i = _clients.find(cid);
    if (i != _clients.end()) {
      i->second->disconnected();
      LOGD(i->second, "session closed");
    }
  }

  void Ui::incoming(uint32_t cid, DynamicJsonDocument *json) {
    if (!json || json->isNull())
      return;
    std::lock_guard<std::mutex> guard(_mutex);
    auto i = _clients.find(cid);
    ui::Client *c;
    if (i == _clients.end()) {
      c = new ui::Client(this, cid);
      _clients[cid] = std::unique_ptr<ui::Client>(c);
      LOGD(c, "session opened");
    } else
      c = i->second.get();
    if (!c->enqueue(json)) {
      JsonObjectConst req = json->as<JsonObjectConst>();
      int seq = req["seq"];
      const char *type = req["type"];
      if (seq && type) {
        char *text = ui::makeResponse(req["name"], type, seq, ui::_errors[0],
                                      true, false);
        wsSend(cid, text);
        free(text);
      }
      delete json;
    } else
      xTaskNotifyGive(_task);
  }

  void Ui::wsSend(const char *text) {
    std::lock_guard<std::mutex> guard(_mutex);
    for (auto it = _clients.begin(); it != _clients.end(); ++it)
      wsSend(it->first, text);
  }

  esp_err_t Ui::wsSend(uint32_t cid, const char *text) {
    std::lock_guard<std::mutex> guard(_sendMutex);
    return _transport->wsSend(cid, text);
  }

  void Ui::run() {
    esp_task_wdt_add(NULL);
    for (;;) {
      esp_task_wdt_reset();
      DynamicJsonDocument *req = nullptr;
      uint32_t cid = 0;
      {
        std::lock_guard<std::mutex> guard(_mutex);
        for (auto it = _clients.begin(); it != _clients.end();) {
          auto client = it->second.get();
          if (client->isDisconnected()) {
            it = _clients.erase(it);
          } else {
            req = client->dequeue();
            cid = it->first;
            if (req)
              break;
            ++it;
          }
        }
      }
      if (req) {
        JsonArrayConst arr = req->as<JsonArrayConst>();
        if (!arr.isNull())
          for (JsonVariantConst v : arr)
            ui::Req::process(this, cid, v.as<JsonObjectConst>());
        else
          ui::Req::process(this, cid, req->as<JsonObjectConst>());
        delete req;
        continue;
      }

      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
  }
}  // namespace esp32m