#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/events/request.hpp"
#include "esp32m/logging.hpp"

namespace esp32m {

  namespace config {
    class Changed;
  }

  class Configurable : public virtual log::Loggable {
   public:
    Configurable(const Configurable &) = delete;
    virtual const char *configName() const {
      return name();
    }

    /** Returns true if this object's configuration was either read and applied
     * from config store or via UI */
    bool isConfigured() {
      return _configured;
    }

   protected:
    Configurable() {}
    virtual bool handleConfigRequest(Request &req);
    virtual bool setConfig(const JsonVariantConst cfg,
                           DynamicJsonDocument **result) {
      return false;
    }
    virtual DynamicJsonDocument *getConfig(RequestContext &ctx) {
      return nullptr;
    }

   private:
    // this flag is modified by config::Changed event
    // request, this flag, otherwise config manager will not
    // recognize config changes
    bool _configured = false;
    friend class config::Changed;
  };

  namespace config {
    class Changed : public Event {
     public:
      Changed(const Changed &) = delete;
      Configurable *configurable() const {
        return _configurable;
      }
      bool saveNow() const {
        return _saveNow;
      }
      static void publish(Configurable *configurable, bool saveNow = false) {
        Changed evt(configurable, saveNow);
        configurable->_configured = true;
        evt.Event::publish();
      }
      static bool is(Event &ev) {
        return ev.is(Type);
      }
      static bool is(Event &ev, Configurable *c)
      {
        return ev.is(Type) && ((Changed &)ev).configurable() == c;
      }

     private:
      Changed(Configurable *configurable, bool saveNow)
          : Event(Type), _configurable(configurable), _saveNow(saveNow) {}
      Configurable *_configurable;
      bool _saveNow;
      constexpr static const char *Type = "config-changed";
    };
  }  // namespace config

}  // namespace esp32m