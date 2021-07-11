#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/events/request.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {
  namespace config {
    bool getMaskSensitive(JsonVariantConst args);
    void setMaskSensitive(JsonVariant args, bool mask = true);
    DynamicJsonDocument* addMaskSensitive(JsonVariantConst args,
                                          bool mask = true);
  }  // namespace config

  class Configurable : public virtual log::Loggable {
   public:
    Configurable(const Configurable&) = delete;
    virtual const char* configName() const {
      return name();
    }

   protected:
    Configurable() {}
    virtual bool handleConfigRequest(Request& req);
    virtual bool setConfig(const JsonVariantConst cfg,
                           DynamicJsonDocument** result) {
      return false;
    }
    virtual DynamicJsonDocument* getConfig(const JsonVariantConst args) {
      return nullptr;
    }
  };

}  // namespace esp32m