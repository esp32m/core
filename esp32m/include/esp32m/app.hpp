#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>

#include "esp32m/config.hpp"
#include "esp32m/config/configurable.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/json.hpp"
#include "esp32m/log/udp.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/props.hpp"

namespace esp32m {

  class App;

  class EventInit : public Event {
   public:
    int level() const {
      return _level;
    }
    static bool is(Event &ev, int level) {
      return ev.is(NAME) && ((EventInit &)ev)._level == level;
    }

   private:
    EventInit(int level) : Event(NAME), _level(level) {}
    int _level;
    static const char *NAME;
    friend class App;
  };

  class EventInited : public Event {
   public:
    static bool is(Event &ev) {
      return ev.is(NAME);
    }

   private:
    EventInited() : Event(NAME) {}
    static const char *NAME;
    friend class App;
  };

  enum class DoneReason { Shutdown, Restart, LightSleep, DeepSleep };
  class EventDone : public Event {
   public:
    static bool is(Event &ev, DoneReason *reason) {
      if (!ev.is(NAME))
        return false;
      if (reason)
        *reason = ((EventDone &)ev)._reason;
      return true;
    }
    static void publish(DoneReason reason);
    DoneReason reason() const {
      return _reason;
    }

   private:
    EventDone(DoneReason reason) : Event(NAME), _reason(reason) {}
    DoneReason _reason;
    static const char *NAME;
  };

  class EventDescribe : public Event {
   public:
    static bool is(Event &ev, EventDescribe **r) {
      if (!ev.is(NAME))
        return false;
      if (r)
        *r = (EventDescribe *)&ev;
      return true;
    }
    void add(const char *name, const JsonVariantConst descriptor) {
      if (descriptor)
        descriptors[name] = descriptor;
    }

   private:
    EventDescribe() : Event(NAME) {}
    std::map<std::string, JsonVariantConst> descriptors;
    static const char *NAME;
    friend class App;
  };

  class AppObject : public virtual log::Loggable, public virtual Configurable {
   public:
    AppObject(const AppObject &) = delete;
    virtual const char *interactiveName() const {
      return name();
    };
    virtual const char *stateName() const {
      return name();
    }

   protected:
    static const char *KeyStateGet;
    static const char *KeyStateSet;
    AppObject();
    virtual bool handleRequest(Request &req);
    virtual void handleEvent(Event &ev){};
    virtual const JsonVariantConst descriptor() const {
      return json::emptyArray();
    };
    virtual bool handleStateRequest(Request &req);
    virtual void setState(const JsonVariantConst cfg,
                          DynamicJsonDocument **result) {}
    virtual DynamicJsonDocument *getState(const JsonVariantConst args) {
      return nullptr;
    }
  };

  class EventStateChanged : public Event {
   public:
    EventStateChanged(const EventStateChanged &) = delete;
    AppObject *object() const {
      return _object;
    }
    static void publish(AppObject *object) {
      EventStateChanged evt(object);
      evt.Event::publish();
    }
    static bool is(Event &ev) {
      return ev.is(Type);
    }

   private:
    EventStateChanged(AppObject *object) : Event(Type), _object(object) {}
    AppObject *_object;
    constexpr static const char *Type = "state-changed";
  };

  class App : public AppObject {
   public:
    class Init {
     public:
      Init(const char *name = nullptr, const char *version = nullptr);
      ~Init();
      void requestInitLevels(uint8_t maxInitLevels);
      void setConfigStore(ConfigStore *store);
      void inferHostname(int limit = 15, int macDigits = 2);
    };
    App(const App &) = delete;
    static App &instance();
    static bool initialized();
    static void restart();
    Config *config() const {
      return _config.get();
    }
    const char *name() const override {
      return _name.c_str();
    }
    const char *interactiveName() const override {
      return "app";
    }
    const char *stateName() const override {
      return "app";
    }
    const char *configName() const override {
      return "app";
    }
    const char *hostname() const {
      return _hostname.c_str();
    }
    const char *version() const {
      return _version;
    }
    Props &props() {
      return _props;
    }
    uint32_t wdtTimeout() const {
      return _wdtTimeout;
    }
    void setHostname(const char *hostname);
    void resetHostname();

   protected:
    DynamicJsonDocument *getState(const JsonVariantConst args) override;
    bool setConfig(const JsonVariantConst cfg,
                   DynamicJsonDocument **result) override;
    DynamicJsonDocument *getConfig(RequestContext &ctx) override;
    void handleEvent(Event &ev) override;
    bool handleRequest(Request &req) override;

   private:
    std::string _name;
    std::string _hostname;
    std::string _defaultHostname;
    const char *_version;
    Props _props;
    uint32_t _sketchSize, _wdtTimeout = 30;
    uint8_t _maxInitLevel = 0;
    uint8_t _curInitLevel = 0;
    std::unique_ptr<Config> _config;
    log::Udp *_udpLogger = nullptr;
    TaskHandle_t _task = nullptr;
    unsigned long _configDirty = 0;
    App(const char *name, const char *verson);
    void init();
    void run();
  };

}  // namespace esp32m