#include "esp32m/config/configurable.hpp"
#include "esp32m/config.hpp"
#include "esp32m/config/changed.hpp"
#include "esp32m/events/broadcast.hpp"
#include "esp32m/json.hpp"

namespace esp32m {
  namespace config {
    bool getMaskSensitive(JsonVariantConst args) {
      return args["options"]["mask_sensitive"] | false;
    }
    void setMaskSensitive(JsonVariant args, bool mask) {
      JsonObject options = args["options"];
      if (!options)
        options = args.createNestedObject("options");
      options["mask_sensitive"] = mask;
    }
    DynamicJsonDocument *addMaskSensitive(JsonVariantConst args, bool mask) {
      DynamicJsonDocument *doc =
          new DynamicJsonDocument(args.memoryUsage() + JSON_OBJECT_SIZE(2));
      JsonVariant root = doc->to<JsonObject>();
      root.set(args);
      setMaskSensitive(root, mask);
      return doc;
    }
  }  // namespace config

  bool Configurable::handleConfigRequest(Request &req) {
    const char *name = req.name();
    if (!strcmp(name, Config::KeyConfigGet)) {
      DynamicJsonDocument *config = getConfig(req.data());
      if (config) {
        json::check(this, config, "getConfig()");
        req.respond(configName(), *config, false);
        delete config;
      }
      return true;
    } else if (!strcmp(name, Config::KeyConfigSet)) {
      JsonVariantConst data =
          req.isBroadcast() ? req.data()[configName()] : req.data();
      if (data.isUnbound())
        return true;
      DynamicJsonDocument *result = nullptr;
      if (setConfig(data, &result)) {
        EventConfigChanged::publish(this);
        Broadcast::publish(configName(), "config-changed");
      }
      if (result) {
        json::check(this, result, "setConfig()");
        req.respond(configName(), *result);
        delete result;
      } else
        req.respond(configName(), json::null<JsonVariantConst>(), false);
      return true;
    }
    return false;
  }

}  // namespace esp32m