#pragma once

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "esp32m/app.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/ui/asset.hpp"
#include "esp32m/ui/client.hpp"
#include "esp32m/ui/transport.hpp"

namespace esp32m {
  namespace ui {
    class Req;
    class Transport;
    struct Auth {
      bool enabled = false;
      std::string username;
      std::string password;
      bool empty() const {
        return username.empty() && password.empty();
      }
    };
  }  // namespace ui

  class Ui : public virtual AppObject {
   public:
    Ui(const Ui&) = delete;
    const char* name() const override {
      return "ui";
    }

    void addTransport(ui::Transport* transport) {
      std::lock_guard<std::mutex> guard(_transportsMutex);
      _transports.emplace_back(transport);

      std::vector<ui::Transport*> snapshot;
      snapshot.reserve(_transports.size());
      for (auto& t : _transports) snapshot.push_back(t.get());
      _transportsView.store(
          std::make_shared<const std::vector<ui::Transport*> >(
              std::move(snapshot)),
          std::memory_order_release);
    }

    std::vector<std::unique_ptr<ui::Asset> >& assets() {
      return _assets;
    }
    void addAsset(ui::Asset* asset) {
      _assets.emplace_back(asset);
    }
    void addAsset(const char* uri, const char* contentType,
                  const uint8_t* start, const uint8_t* end,
                  const char* contentEncoding, const char* etag) {
      ui::AssetInfo info =
          ui::AssetInfo{uri, contentType, contentEncoding, etag};

      addAsset(new ui::MemoryAsset(info, start, end - start));
    }

    const ui::Auth& auth() const {
      return _auth;
    }

    static Ui& instance() {
      static Ui instance;
      return instance;
    }

   protected:
    //    bool handleRequest(Request &req) override;
    void handleEvent(Event& ev) override;
    void broadcast(const char* text);
    bool setConfig(RequestContext& ctx) override {
      JsonObjectConst root = ctx.data.as<JsonObjectConst>();
      auto auth = root["auth"];
      bool changed = false;
      if (auth) {
        json::from(auth["enabled"], _auth.enabled, &changed);
        json::from(auth["username"], _auth.username, &changed);
        json::from(auth["password"], _auth.password, &changed);
      }
      return changed;
    }
    JsonDocument* getConfig(RequestContext& ctx) override {
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      if (!_auth.empty()) {
        auto auth = root["auth"].to<JsonObject>();
        json::to(auth, "enabled", _auth.enabled);
        json::to(auth, "username", _auth.username);
        json::to(auth, "password", _auth.password);
      }
      return doc;
    }

   private:
    Ui() {
      _transportsView.store(
          std::make_shared<const std::vector<ui::Transport*> >(),
          std::memory_order_release);
    }

    mutable std::mutex _transportsMutex;
    ui::Auth _auth;
    TaskHandle_t _task = nullptr;
    std::vector<std::unique_ptr<ui::Transport> > _transports;
    std::atomic<std::shared_ptr<const std::vector<ui::Transport*> > >
        _transportsView;
    std::vector<std::unique_ptr<ui::Asset> > _assets;
    void run();
    void notifyIncoming() {
      if (_task)
        xTaskNotifyGive(_task);
    }
    friend class ui::Req;
    friend class ui::Transport;
  };

}  // namespace esp32m
