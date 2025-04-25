#pragma once

#include <deque>
#include <mutex>

#include <ArduinoJson.h>
#include <esp32m/logging.hpp>
#include <esp32m/ui.hpp>

namespace esp32m {
  class Ui;
  namespace ui {

    static bool requestsSame(JsonDocument *a, JsonDocument *b) {
      if (!a && !b)
        return true;
      if (!a || !b)
        return false;
      auto oa = a->as<JsonObjectConst>();
      auto ob = a->as<JsonObjectConst>();
      if (oa.isNull() && ob.isNull())
        return true;
      if (oa.isNull() || ob.isNull())
        return false;
      return oa["type"] == ob["type"] && oa["name"] == ob["name"] &&
             oa["target"] == ob["target"];
    }

    class Client : public log::Loggable {
     public:
      Client(esp32m::Ui *ui, uint32_t id);
      Client(const Client &) = delete;
      const char *name() const override {
        return _name.c_str();
      }
      void disconnected() {
        _disconnected = true;
      }
      bool isDisconnected() const {
        return _disconnected;
      }
      bool enqueue(JsonDocument *req) {
        std::lock_guard<std::mutex> guard(_mutex);
        // http client may open new session with the id of the previous session that was closed just before.
        // So the new Client will not be created, but the old one will be re-used.
        // But, this client already received disconnected() call and will be removed by the Ui thread
        // This is a hack to prevent removal, assuming that if we got a message for this client, it is not disconnected
        _disconnected = false; 
        auto size = _requests.size();
        if (size > 3) {
          if (sameRequestInQueue(req)) {
            logW("duplicate request");
            return false;
          }
          if (size > 10) {
            logW("too many requests");
            return false;
          }
        }
        /*char *json = json::allocSerialize(req->as<JsonVariantConst>());
        logD("enqueue %s", json);
        free(json);*/

        _requests.push_back(std::unique_ptr<JsonDocument>(req));
        return true;
      }
      JsonDocument *dequeue() {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_requests.size()) {
          JsonDocument *result = _requests.front().release();
          _requests.pop_front();
          return result;
        }
        return nullptr;
      }

     private:
      std::string _name;
      bool _disconnected = false;
      std::deque<std::unique_ptr<JsonDocument> > _requests;
      std::mutex _mutex;
      bool sameRequestInQueue(JsonDocument *doc) {
        // must be called from scope guarded by mutex
        for (auto it = _requests.begin(); it != _requests.end(); ++it)
          if (requestsSame(it->get(), doc))
            return true;
        return false;
      }
    };

  }  // namespace ui
}  // namespace esp32m
