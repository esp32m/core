#pragma once

#include <atomic>
#include <deque>
#include <mutex>

#include <ArduinoJson.h>
#include <esp32m/logging.hpp>

namespace esp32m {
  class Ui;
  namespace ui {
    
    class Transport;

    static bool requestsSame(JsonDocument *a, JsonDocument *b) {
      if (!a && !b)
        return true;
      if (!a || !b)
        return false;
      if (a->is<JsonArrayConst>() || b->is<JsonArrayConst>())
        return a->is<JsonArrayConst>() && b->is<JsonArrayConst>() &&
               a->as<JsonArrayConst>() == b->as<JsonArrayConst>();
      auto oa = a->as<JsonObjectConst>();
      auto ob = b->as<JsonObjectConst>();
      if (oa.isNull() && ob.isNull())
        return true;
      if (oa.isNull() || ob.isNull())
        return false;
      return oa == ob;
    }

    class Client : public log::Loggable {
     public:
      Client(Transport *transport, uint32_t id, uint64_t generation);
      Client(const Client &) = delete;
      const char *name() const override {
        return _name.c_str();
      }
      void disconnected() {
        _disconnected.store(true, std::memory_order_release);
      }
      bool isDisconnected() const {
        return _disconnected.load(std::memory_order_acquire);
      }
      bool enqueue(JsonDocument *req) {
        std::lock_guard<std::mutex> guard(_mutex);
        if (isDisconnected())
          return false;
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
      uint64_t generation() const { return _generation; }
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
      uint64_t _generation;
      std::atomic_bool _disconnected{false};
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
