#pragma once

#include <mutex>
#include <queue>

#include <ArduinoJson.h>

namespace esp32m {
  namespace ui {

    class Client {
     public:
      void disconnected() {
        _disconnected = true;
      }
      bool isDisconnected() const {
        return _disconnected;
      }
      bool enqueue(DynamicJsonDocument *req) {
        std::lock_guard<std::mutex> guard(_mutex);
        auto size = _requests.size();
        if (size > 1) {
          if (size > 5 || _requests.back()->as<JsonObjectConst>()["type"] ==
                              req->as<JsonObjectConst>()["type"])
            return false;
        }
        _requests.push(std::unique_ptr<DynamicJsonDocument>(req));
        return true;
      }
      DynamicJsonDocument *dequeue() {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_requests.size()) {
          DynamicJsonDocument *result = _requests.front().release();
          _requests.pop();
          return result;
        }
        return nullptr;
      }

     private:
      bool _disconnected = false;
      std::queue<std::unique_ptr<DynamicJsonDocument> > _requests;
      std::mutex _mutex;
    };

  }  // namespace ui
}  // namespace esp32m
