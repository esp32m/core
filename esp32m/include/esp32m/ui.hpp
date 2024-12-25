#pragma once

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <map>
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
    Ui(ui::Transport *transport);
    Ui(const Ui &) = delete;
    ~Ui();
    const char *name() const override {
      return "ui";
    }
    ui::Transport *transport() const {
      return _transport;
    }
    std::vector<ui::Asset> &assets() {
      return _assets;
    }
    void addAsset(const char *uri, const char *contentType,
                  const uint8_t *start, const uint8_t *end,
                  const char *contentEncoding, const char *etag);
    const ui::Auth &auth() const {
      return _auth;
    }

    static Ui &instance();

   protected:
    //    bool handleRequest(Request &req) override;
    void handleEvent(Event &ev) override;
    void wsSend(const char *text);
    esp_err_t wsSend(uint32_t cid, const char *text);
    bool setConfig(const JsonVariantConst cfg,
                   DynamicJsonDocument **result) override {
      JsonObjectConst root = cfg.as<JsonObjectConst>();
      auto auth = root["auth"];
      bool changed = false;
      if (auth) {
        json::from(auth["enabled"], _auth.enabled, &changed);
        json::from(auth["username"], _auth.username, &changed);
        json::from(auth["password"], _auth.password, &changed);
      }
      return changed;
    }
    DynamicJsonDocument *getConfig(RequestContext &ctx) override {
      size_t size = JSON_OBJECT_SIZE(4)  // auth: enabled, username, password
                    + JSON_STRING_SIZE(_auth.username.size()) +
                    JSON_STRING_SIZE(_auth.password.size());
      auto doc = new DynamicJsonDocument(size);
      auto root = doc->to<JsonObject>();
      if (!_auth.empty()) {
        auto auth = root.createNestedObject("auth");
        json::to(auth, "enabled", _auth.enabled);
        json::to(auth, "username", _auth.username);
        json::to(auth, "password", _auth.password);
      }
      return doc;
    }

   private:
    std::mutex _mutex, _sendMutex;
    ui::Auth _auth;
    TaskHandle_t _task;
    ui::Transport *_transport;
    std::map<uint32_t, std::unique_ptr<ui::Client> > _clients;
    std::vector<ui::Asset> _assets;
    void run();
    void incoming(uint32_t cid, DynamicJsonDocument *json);
    void sessionClosed(uint32_t cid);
    friend class ui::Req;
    friend class ui::Transport;
  };

}  // namespace esp32m
