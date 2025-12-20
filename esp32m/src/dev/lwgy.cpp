#include "esp32m/dev/lwgy.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

#include "math.h"

namespace esp32m {
  namespace dev {

    float fromCDAB(uint16_t *ptr) {
      float res;
      ((uint16_t *)&res)[0] = ptr[0];
      ((uint16_t *)&res)[1] = ptr[1];
      return res;
    }
    void toCDAB(uint16_t *ptr, float v) {
      ptr[0] = ((uint16_t *)&v)[0];
      ptr[1] = ((uint16_t *)&v)[1];
    }

    Lwgy::Lwgy(uint8_t addr, const char *name)
        : _name(name ? name : "LWGY"),
          _addr(addr),
          _flow(this, "volume_flow_rate"),
          _consumption(this, "water") {
      auto group = sensor::nextGroup();
      _consumption.group = group;
      _consumption.stateClass = sensor::StateClass::Total;
      _consumption.unit = "m³";
      _flow.group = group;
      _flow.unit = "m³/h";
      _flow.stateClass = sensor::StateClass::Measurement;
      Device::init(Flags::HasSensors);
    }

    bool Lwgy::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    bool Lwgy::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("reset")) {
        modbus::Master &mb = modbus::Master::instance();
        if (!mb.isRunning())
          req.respond(ESP_ERR_INVALID_STATE);
        int16_t v = 1;
        req.respond(
            mb.request(_addr, modbus::Command::WriteRegister, 0x200, 1, &v));
        return true;
      }
      return false;
    }

    JsonDocument *Lwgy::getState(RequestContext &ctx) {
      JsonDocument *doc = new JsonDocument(); /* JSON_ARRAY_SIZE(10) */
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      arr.add(_addr);
      arr.add(_flow.get());
      arr.add(_consumption.get());
      arr.add(_id);
      arr.add(_decimals);
      arr.add(_density);
      arr.add(_flowunit);
      arr.add(_range);
      arr.add(_mc);
      return doc;
    }

    bool Lwgy::setConfig(RequestContext &ctx) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      uint16_t regs[11] = {};
      uint16_t cdab[2] = {};
      auto res =
          mb.request(_addr, modbus::Command::ReadHolding, 0x73, 11, regs);
      if (res != ESP_OK)
        return false;
      bool changed = false;
      auto v = regs[0];
      auto cfg=ctx.data.as<JsonObjectConst>();
      if (json::from(cfg["um"], v, &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x73, 1, &v);
      v = regs[1];
      if (json::from(cfg["ud"], v, &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x74, 1, &v);
      v = regs[2];
      if (json::from(cfg["ucf"], v, &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x75, 1, &v);
      v = regs[3];
      if (json::from(cfg["ums"], v, &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x76, 1, &v);
      v = regs[4];
      if (json::from(cfg["urd"], v, &changed))
        mb.request(_addr, modbus::Command::WriteRegister, 0x77, 1, &v);
      auto fv = fromCDAB(&regs[5]);
      if (json::from(cfg["range"], fv, &changed)) {
        toCDAB(cdab, cfg["range"]);
        mb.request(_addr, modbus::Command::WriteRegister, 0x78, 2, cdab);
      }
      fv = fromCDAB(&regs[7]);
      if (json::from(cfg["sse"], fv, &changed)) {
        toCDAB(cdab, cfg["sse"]);
        mb.request(_addr, modbus::Command::WriteRegister, 0x7A, 2, cdab);
      }
      fv = fromCDAB(&regs[9]);
      if (json::from(cfg["dt"], fv, &changed)) {
        toCDAB(cdab, cfg["dt"]);
        mb.request(_addr, modbus::Command::WriteRegister, 0x7C, 2, cdab);
      }
      res = mb.request(_addr, modbus::Command::ReadHolding, 0x90, 4, regs);
      if (res == ESP_OK) {
        fv = fromCDAB(&regs[0]);
        if (json::from(cfg["fd"], fv, &changed)) {
          toCDAB(cdab, cfg["fd"]);
          mb.request(_addr, modbus::Command::WriteRegister, 0x90, 2, cdab);
        }
        fv = fromCDAB(&regs[2]);
        if (json::from(cfg["sc"], fv, &changed)) {
          toCDAB(cdab, cfg["sc"]);
          mb.request(_addr, modbus::Command::WriteRegister, 0x92, 2, cdab);
        }
      }
      res = mb.request(_addr, modbus::Command::ReadHolding, 0xa2, 2, regs);
      if (res == ESP_OK) {
        fv = fromCDAB(&regs[0]);
        if (json::from(cfg["mc"], fv, &changed)) {
          toCDAB(cdab, cfg["mc"]);
          mb.request(_addr, modbus::Command::WriteRegister, 0xa2, 2, cdab);
        }
      }
      return changed;
    }

    JsonDocument *Lwgy::getConfig(RequestContext &ctx) {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return nullptr;
      uint16_t regs[11] = {};
      auto res =
          mb.request(_addr, modbus::Command::ReadHolding, 0x73, 11, regs);
      if (res != ESP_OK)
        return nullptr;

      JsonDocument *doc = new JsonDocument(); /* JSON_OBJECT_SIZE(11) */
      auto root = doc->to<JsonObject>();
      json::to(root, "um", regs[0]);
      json::to(root, "ud", regs[1]);
      json::to(root, "ucf", regs[2]);
      json::to(root, "ums", regs[3]);
      json::to(root, "urd", regs[4]);
      json::to(root, "range", fromCDAB(&regs[5]));
      json::to(root, "sse", fromCDAB(&regs[7]));
      json::to(root, "dt", fromCDAB(&regs[9]));
      res = mb.request(_addr, modbus::Command::ReadHolding, 0x90, 4, regs);
      if (res == ESP_OK) {
        json::to(root, "fd", fromCDAB(&regs[0]));
        json::to(root, "sc", fromCDAB(&regs[2]));
      }
      res = mb.request(_addr, modbus::Command::ReadHolding, 0xa2, 2, regs);
      if (res == ESP_OK)
        json::to(root, "mc", fromCDAB(&regs[0]));
      return doc;
    }

    bool Lwgy::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      uint16_t values[7] = {};
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 0x00, 7, values));
      _decimals = values[0];
      _flowunit = values[1];
      _density = values[2];
      _range = fromCDAB(&values[3]);
      _mc = fromCDAB(&values[5]);
      ESP_CHECK_RETURN_BOOL(
          mb.request(_addr, modbus::Command::ReadHolding, 0x100, 4, values));
      _stamp = millis();
      _consumption.set(fromCDAB(&values[0]));
      _flow.set(fromCDAB(&values[2]));
      return true;
    }

    Lwgy *useLwgy(uint8_t addr, const char *name) {
      return new Lwgy(addr, name);
    }

  }  // namespace dev
}  // namespace esp32m