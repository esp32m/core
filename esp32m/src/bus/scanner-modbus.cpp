#include <esp_task_wdt.h>

#include <ArduinoJson.h>

#include "esp32m/app.hpp"
#include "esp32m/base.hpp"
#include "esp32m/config/changed.hpp"
#include "esp32m/events.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/events/response.hpp"
#include "esp32m/json.hpp"

#include "esp32m/bus/modbus.hpp"
#include "esp32m/bus/scanner/modbus.hpp"

namespace esp32m {
  namespace bus {
    namespace scanner {
      Modbus::Modbus() {}

      DynamicJsonDocument *Modbus::getConfig(const JsonVariantConst args) {
        DynamicJsonDocument *doc =
            new DynamicJsonDocument(JSON_OBJECT_SIZE(10));
        auto root = doc->to<JsonObject>();
        root["mode"] = (_ascii ? "ascii" : "rtu");
        root["from"] = _startAddr;
        root["to"] = _endAddr;
        root["uart"] = _uart;
        root["baud"] = _baud;
        root["parity"] = _parity;
        root["addr"] = _addr;
        if (_cmd)
          root["cmd"] = _cmd;
        if (_regc) {
          root["regs"] = _regs;
          root["regc"] = _regc;
        }
        return doc;
      }

      bool Modbus::setConfig(const JsonVariantConst data,
                             DynamicJsonDocument **result) {
        bool changed = false;
        bool ascii = data["mode"] == "ascii";
        if (_ascii != ascii) {
          _ascii = ascii;
          changed = true;
        }
        json::compareSet(_startAddr, data["from"], changed);
        json::compareSet(_endAddr, data["to"], changed);
        json::compareSet(_addr, data["addr"], changed);
        json::compareSet(_uart, data["uart"], changed);
        json::compareSet(_baud, data["baud"], changed);
        json::compareSet((int &)_parity, data["parity"], changed);
        json::compareSet(_regs, data["regs"], changed);
        json::compareSet(_regc, data["regc"], changed);
        json::compareSet((int &)_cmd, data["cmd"], changed);
        if (_startAddr < 1) {
          _startAddr = 1;
          changed = true;
        }
        if (_endAddr > 247) {
          _endAddr = 247;
          changed = true;
        }
        if (_endAddr < _startAddr) {
          _endAddr = _startAddr;
          changed = true;
        }
        if (_addr < 1) {
          _addr = 1;
          changed = true;
        }
        return changed;
      }

      bool Modbus::handleRequest(Request &req) {
        if (AppObject::handleRequest(req))
          return true;
        if (req.is("scan") || req.is("request")) {
          auto data = req.data();
          if (_pendingResponse)
            req.respond(ESP_ERR_INVALID_STATE);
          else {
            setConfig(data, nullptr);
            _pendingResponse = req.makeResponse();
            EventConfigChanged::publish(this, true);
            xTaskNotifyGive(_task);
          }
          return true;
        }
        return false;
      }

      bool Modbus::handleEvent(Event &ev) {
        if (EventInit::is(ev, 0)) {
          xTaskCreate([](void *self) { ((Modbus *)self)->run(); }, "m/s/modbus",
                      4096, this, 1, &_task);
          return true;
        }
        return false;
      }

      Modbus &Modbus::instance() {
        static Modbus i;
        return i;
      }

      void Modbus::run() {
        esp_task_wdt_add(NULL);
        for (;;) {
          esp_task_wdt_reset();
          if (_pendingResponse) {
            memset(&_addrs, 0, sizeof(_addrs));
            modbus::Master &mb = modbus::Master::instance();
            esp_err_t err = ESP_OK;
            mb.configureSerial(_uart, _baud, _parity, _ascii);
            if (!mb.isRunning())
              err = mb.start();
            if (mb.isRunning()) {
              if (_pendingResponse->is("scan")) {
                auto t = millis();

                for (int i = _startAddr; i <= _endAddr; i++) {
                  uint16_t reg;
                  err = mb.request(i, modbus::Command::ReadInput, 0, 1, &reg);
                  if (err != ESP_OK)
                    err =
                        mb.request(i, modbus::Command::ReadHolding, 0, 1, &reg);
                  if (err == ESP_OK) {
                    logI("found modbus device at 0x%02x", i);
                    _addrs[i >> 3] |= 1 << (i & 7);
                  }
                  if (millis() - t > 1000) {
                    t = millis();
                    DynamicJsonDocument *progressDoc =
                        new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
                    progressDoc->to<JsonObject>()["progress"] =
                        (i - _startAddr) * 100 / (_endAddr - _startAddr);
                    _pendingResponse->setPartial(true);
                    _pendingResponse->setData(progressDoc);
                    _pendingResponse->publish();
                  }
                  esp_task_wdt_reset();
                }
                DynamicJsonDocument *doc = new DynamicJsonDocument(
                    JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(sizeof(_addrs)));
                auto addrs = doc->createNestedArray("addrs");
                for (auto i = 0; i < sizeof(_addrs); i++) addrs.add(_addrs[i]);
                _pendingResponse->setPartial(false);
                _pendingResponse->setData(doc);
                _pendingResponse->publish();
              } else if (_pendingResponse->is("request") && _cmd && _regc) {
                void *buf = malloc(_regc * 2);
                err = mb.request(_addr, _cmd, _regs, _regc, buf);
                DynamicJsonDocument *doc = new DynamicJsonDocument(
                    JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(_regc));
                if (err == ESP_OK) {
                  auto regs = doc->createNestedArray("regs");
                  for (auto i = 0; i < _regc; i++)
                    regs.add(((uint16_t *)buf)[i]);
                  _pendingResponse->setData(doc);
                } else
                  _pendingResponse->setError(err);
                free(buf);
                _pendingResponse->publish();
              }
            }
            delete _pendingResponse;
            _pendingResponse = nullptr;
          }
          ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        }
      }

      Modbus *useModbus() {
        return &Modbus::instance();
      }

    }  // namespace scanner
  }    // namespace bus
}  // namespace esp32m