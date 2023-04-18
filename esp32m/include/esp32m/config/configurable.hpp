#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/events/request.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {

  class Configurable : public virtual log::Loggable {
   public:
    Configurable(const Configurable&) = delete;
    virtual const char* configName() const {
      return name();
    }

    /** Returns true if this object's configuration was either read and applied
     * from config store or via UI */
    bool isConfigured() {
      return _configured;
    }

   protected:
    Configurable() {}
    virtual bool handleConfigRequest(Request& req);
    virtual bool setConfig(const JsonVariantConst cfg,
                           DynamicJsonDocument** result) {
      return false;
    }
    virtual DynamicJsonDocument* getConfig(RequestContext &ctx) {
      return nullptr;
    }

   private:
    bool _configured = false;
  };

}  // namespace esp32m