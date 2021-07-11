#include <esp_task_wdt.h>

#include <ArduinoJson.h>

#include "esp32m/app.hpp"
#include "esp32m/events.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/json.hpp"

#include "esp32m/bus/scanner/owb.hpp"

namespace esp32m {
  namespace bus {
    namespace scanner {

      DynamicJsonDocument *Owb::getState(const JsonVariantConst args) {
        DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(2));
        auto root = doc->to<JsonObject>();
        root["pin"] = _pin;
        root["max"] = _maxDevices;
        return doc;
      }

      bool Owb::handleRequest(Request &req) {
        if (AppObject::handleRequest(req))
          return true;
        if (req.is("owb", "scan")) {
          auto data = req.data();
          if (_pendingResponse)
            req.respond(ESP_FAIL);
          else {
            _pin = data["pin"] | 4;
            _maxDevices = data["max"] | 16;
            if (_maxDevices < 4)
              _maxDevices = 4;
            _pendingResponse = req.makeResponse();
            xTaskNotifyGive(_task);
          }
          return true;
        }
        return false;
      }

      bool Owb::handleEvent(Event &ev) {
        if (EventInit::is(ev, 0)) {
          xTaskCreate([](void *self) { ((Owb *)self)->run(); }, "m/s/owb", 4096,
                      this, 1, &_task);
          return true;
        }
        return false;
      }

      Owb &Owb::instance() {
        static Owb i;
        return i;
      }

      void Owb::run() {
        esp_task_wdt_add(NULL);
        for (;;) {
          esp_task_wdt_reset();
          if (_pendingResponse) {
            DynamicJsonDocument *doc = new DynamicJsonDocument(
                JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(_maxDevices) +
                _maxDevices * 17);
            auto root = doc->to<JsonObject>();
            auto codes = root.createNestedArray("codes");
            esp32m::Owb owb((gpio_num_t)_pin);
            int num_devices = 0;
            std::lock_guard guard(owb.mutex());
            owb::Search search(&owb);
            owb::ROMCode *addr;
            while ((addr = search.next())) {
              char rom_code_s[17];
              owb::toString(*addr, rom_code_s, sizeof(rom_code_s));
              codes.add(rom_code_s);
              ++num_devices;
              logI("found owb device #%d : %s\n", num_devices, rom_code_s);
            }
            auto error = search.error();
            if (error == ESP_OK)
              _pendingResponse->setData(doc);
            else
              _pendingResponse->setError(error);
            _pendingResponse->publish();
            delete _pendingResponse;
            _pendingResponse = nullptr;
          }
          ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        }
      }

      Owb *useOwb() {
        return &Owb::instance();
      }

    }  // namespace scanner
  }    // namespace bus
}  // namespace esp32m