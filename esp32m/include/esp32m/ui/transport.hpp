#pragma once

#include "esp32m/logging.hpp"

#include <atomic>
#include <ArduinoJson.h>
#include <esp_err.h>
#include <esp32m/ui/client.hpp>
#include <esp32m/ui/payload.hpp>
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
      virtual esp_err_t sendSession(uint32_t cid, uint64_t generation,
                                   Payload text) {
        if (!text) {
          closeSession(cid, generation);
          return ESP_ERR_NO_MEM;
        }
        return sessionAlive(cid, generation) ? sendTo(cid, text.c_str())
                                             : ESP_ERR_INVALID_STATE;
      }
      virtual void closeSession(uint32_t cid, uint64_t generation);
      bool sessionAlive(uint32_t cid, uint64_t generation);
      uint64_t sessionGeneration(uint32_t cid);
      virtual void sendStats(JsonObject result) {}

      void broadcast(const char* text) {
        auto ids = _clientIdsView.load(std::memory_order_acquire);
        if (!ids)
          return;
        for (auto cid : *ids)
          if (sessionGeneration(cid)) sendTo(cid, text);
      }

     protected:
      Ui* _ui = nullptr;
      std::mutex _clientsMutex;
      std::map<uint32_t, std::unique_ptr<Client> > _clients;
      void incoming(uint32_t cid, void* data, size_t len, uint64_t generation = 0);
      void incoming(uint32_t cid, JsonDocument* json, uint64_t generation = 0);
      void sessionClosed(uint32_t cid);
      uint64_t sessionOpened(uint32_t cid);
      virtual void init(Ui* ui) {
        _ui = ui;
        _clientIdsView.store(std::make_shared<const std::vector<uint32_t>>(),
                             std::memory_order_release);
      };

      std::atomic<std::shared_ptr<const std::vector<uint32_t>>> _clientIdsView;

     private:
      void process();
      void refreshClientIdsViewLocked();
      uint64_t _nextGeneration = 0;

      friend class esp32m::Ui;
      friend class Req;
    };

  }  // namespace ui
}  // namespace esp32m
