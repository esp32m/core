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
  }  // namespace ui

  class Ui : public virtual AppObject {
   public:
    Ui(ui::Transport *transport);
    Ui(const Ui &) = delete;
    ~Ui();
    const char *name() const override {
      return "ui";
    }
    std::vector<ui::Asset> &assets() {
      return _assets;
    }
    void addAsset(const char *uri, const char *contentType,
                  const uint8_t *start, const uint8_t *end,
                  const char *contentEncoding, const char *etag);
    static Ui &instance();

   protected:
    //    bool handleRequest(Request &req) override;
    bool handleEvent(Event &ev) override;
    void wsSend(const char *text);
    esp_err_t wsSend(uint32_t cid, const char *text);

   private:
    std::mutex _mutex, _sendMutex;
    TaskHandle_t _task;
    ui::Transport *_transport;
    std::map<uint32_t, ui::Client> _clients;
    std::vector<ui::Asset> _assets;
    void run();
    void incoming(uint32_t cid, DynamicJsonDocument *json);
    void sessionClosed(uint32_t cid);
    friend class ui::Req;
    friend class ui::Transport;
  };

}  // namespace esp32m
