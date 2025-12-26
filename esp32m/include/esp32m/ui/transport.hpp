#pragma once

#include "esp32m/logging.hpp"

#include <atomic>
#include <ArduinoJson.h>
#include <esp_err.h>
#include <esp32m/ui/client.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace esp32m {
  class Ui;

  namespace ui {

    class Transport : public log::Loggable {
     public:
      virtual ~Transport() = default;
      virtual esp_err_t sendTo(uint32_t cid, const char* text) = 0;

      void broadcast(const char* text) {
        auto ids = _clientIdsView.load(std::memory_order_acquire);
        if (!ids)
          return;
        for (auto cid : *ids)
          sendTo(cid, text);
      }

     protected:
      Ui* _ui = nullptr;
      std::mutex _clientsMutex;
      std::map<uint32_t, std::unique_ptr<Client> > _clients;
      void incoming(uint32_t cid, void* data, size_t len);
      void incoming(uint32_t cid, JsonDocument* json);
      void sessionClosed(uint32_t cid);
      virtual void init(Ui* ui) {
        _ui = ui;
        _clientIdsView.store(std::make_shared<const std::vector<uint32_t>>(),
                             std::memory_order_release);
      };

      std::atomic<std::shared_ptr<const std::vector<uint32_t>>> _clientIdsView;

     private:
      void process();
      void refreshClientIdsViewLocked();

      friend class esp32m::Ui;
    };

  }  // namespace ui
}  // namespace esp32m
