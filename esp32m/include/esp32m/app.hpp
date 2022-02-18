#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>

#include "esp32m/config.hpp"
#include "esp32m/config/configurable.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/logging.hpp"

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

  class Stateful : public virtual log::Loggable {
   public:
    Stateful(const Stateful &) = delete;
    virtual const char *stateName() const {
      return name();
    }

   protected:
    static const char *KeyStateGet;
    static const char *KeyStateSet;
    Stateful() {}
    virtual bool handleStateRequest(Request &req);
    virtual void setState(const JsonVariantConst cfg,
                          DynamicJsonDocument **result) {}
    virtual DynamicJsonDocument *getState(const JsonVariantConst args) {
      return nullptr;
    }
  };

  class Interactive : public virtual INamed {
   public:
    Interactive(const Interactive &) = delete;
    virtual const char *interactiveName() const {
      return name();
    };

   protected:
    Interactive();
    virtual bool handleRequest(Request &req) = 0;
    virtual bool handleEvent(Event &ev) {
      return false;
    };
  };

  class AppObject : public virtual log::Loggable,
                    public virtual Interactive,
                    public virtual Configurable,
                    public virtual Stateful {
   public:
    AppObject(const Interactive &) = delete;

   protected:
    AppObject(){};
    bool handleRequest(Request &req) override;
  };

  class App : public AppObject {
   public:
    class Init {
     public:
      Init(const char *name = nullptr, const char *version = nullptr);
      ~Init();
      void requestInitLevels(uint8_t maxInitLevels);
      void setConfigStore(ConfigStore *store);
    };
    App(const App &) = delete;
    static App &instance();
    static bool initialized();
    static void restart();
    Config *config() const {
      return _config.get();
    }
    const char *name() const override {
      return _name;
    }
    const char *interactiveName() const override {
      return "app";
    }
    const char *stateName() const override {
      return "app";
    }
    uint32_t wdtTimeout() const {
      return _wdtTimeout;
    }

   protected:
    DynamicJsonDocument *getState(const JsonVariantConst args) override;
    bool handleEvent(Event &ev) override;
    bool handleRequest(Request &req) override;

   private:
    const char *_name;
    const char *_version;
    uint32_t _sketchSize, _wdtTimeout = 30;
    uint8_t _maxInitLevel = 0;
    uint8_t _curInitLevel = 0;
    std::unique_ptr<Config> _config;
    TaskHandle_t _task = nullptr;
    unsigned long _configDirty = 0;
    App(const char *name, const char *verson);
    void init();
    void run();
  };

}  // namespace esp32m