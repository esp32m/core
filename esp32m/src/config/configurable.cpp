#include "esp32m/config/configurable.hpp"
#include "esp32m/config.hpp"
#include "esp32m/config/changed.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/json.hpp"

namespace esp32m {

  bool Configurable::handleConfigRequest(Request &req) {
    const char *name = req.name();
    const char *cname = configName();
    JsonVariantConst reqData = req.data();
    bool internalRequest = !req.origin();
    if (!strcmp(name, Config::KeyConfigGet)) {
      // internal requests means request to save config
      // we don't want to save default config (the one that never changed)
      if (internalRequest && !_configured)
        return true;
      RequestContext ctx(req, reqData);
      DynamicJsonDocument *config = getConfig(ctx);
      if (config) {
        json::check(this, config, "getConfig()");
        req.respond(cname, *config, false);
        delete config;
      }
      return true;
    } else if (!strcmp(name, Config::KeyConfigSet)) {
      JsonVariantConst data = req.isBroadcast() ? reqData[cname] : reqData;
      if (data.isUnbound())
        return true;
      RequestContext ctx(req, data);
      DynamicJsonDocument *result = nullptr;
      if (setConfig(data, &result)) {
        EventConfigChanged::publish(this);
        Broadcast::publish(cname, "config-changed");
        _configured = true;
      }
      if (result) {
        json::check(this, result, "setConfig()");
        req.respond(cname, *result);
        delete result;
      } else
        req.respond(cname, json::null<JsonVariantConst>(), false);
      return true;
    }
    return false;
  }

}  // namespace esp32m