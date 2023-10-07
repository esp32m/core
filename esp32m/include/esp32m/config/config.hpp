#pragma once

#include <memory>
#include <mutex>

#include "esp32m/config/store.hpp"
#include "esp32m/events.hpp"

namespace esp32m {
  
  class AppObject;
  class Config;

  namespace config {

    class Changed : public Event {
     public:
      Changed(const Changed &) = delete;
      AppObject *configurable() const {
        return _configurable;
      }
      bool saveNow() const {
        return _saveNow;
      }
      static void publish(AppObject *configurable, bool saveNow = false);
      static bool is(Event &ev) {
        return ev.is(Type);
      }
      static bool is(Event &ev, AppObject *c) {
        return ev.is(Type) && ((Changed &)ev).configurable() == c;
      }

     private:
      Changed(AppObject *configurable, bool saveNow)
          : Event(Type), _configurable(configurable), _saveNow(saveNow) {}
      AppObject *_configurable;
      bool _saveNow;
      constexpr static const char *Type = "config-changed";
    };

    class Store : public log::Loggable {
     protected:
      virtual void write(const DynamicJsonDocument& config) = 0;
      virtual DynamicJsonDocument* read() = 0;
      virtual void reset() = 0;
      friend class esp32m::Config;
    };

  }  // namespace config

  class Config : public virtual log::Loggable {
   public:
    static const char *KeyConfigGet;
    static const char *KeyConfigSet;

    Config(config::Store *store) : _store(store){};
    Config(const Config &) = delete;
    const char *name() const override {
      return "config";
    }
    void save();
    void load();
    void reset();
    DynamicJsonDocument *read();

   private:
    std::unique_ptr<config::Store> _store;
    std::mutex _mutex;
  };

}  // namespace esp32m