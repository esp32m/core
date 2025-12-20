#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>

#include "esp32m/config/config.hpp"
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
      return ev.is(Type) && ((EventInit &)ev)._level == level;
    }

   private:
    EventInit(int level) : Event(Type), _level(level) {}
    int _level;
    constexpr static const char *Type = "init";
    friend class App;
  };

  class EventInited : public Event {
   public:
    static bool is(Event &ev) {
      return ev.is(Type);
    }

   private:
    EventInited() : Event(Type) {}
    constexpr static const char *Type = "inited";
    friend class App;
  };

  enum class DoneReason { Shutdown, Restart, LightSleep, DeepSleep };
  class EventDone : public Event {
   public:
    static bool is(Event &ev, DoneReason *reason) {
      if (!ev.is(Type))
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
    EventDone(DoneReason reason) : Event(Type), _reason(reason) {}
    DoneReason _reason;
    constexpr static const char *Type = "done";
  };

  class EventDescribe : public Event {
   public:
    static bool is(Event &ev, EventDescribe **r) {
      if (!ev.is(Type))
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
    EventDescribe() : Event(Type) {}
    std::map<std::string, JsonVariantConst> descriptors;
    constexpr static const char *Type = "describe";
    friend class App;
  };

  /** This is fired from the APP task on irregular intervals around 1 sec to
   * perform various periodic jobs */
  class EventPeriodic : public Event {
   public:
    static bool is(Event &ev) {
      return ev.is(Type);
    }

   private:
    EventPeriodic() : Event(Type) {}
    constexpr static const char *Type = "periodic";
    friend class App;
  };

  class AppObject : public virtual log::Loggable {
   public:
    AppObject(const AppObject &) = delete;
    virtual ~AppObject() {
      if (_subscription)
        delete _subscription;
    }
    virtual const char *interactiveName() const {
      return name();
    };

    /** Returns true if this object's configuration was read and applied
     * either from config store or via UI */
    bool isConfigured() {
      return _configured;
    }

   protected:
    static constexpr const char *KeyStateGet = "state-get";
    static constexpr const char *KeyStateSet = "state-set";
    static constexpr const char *KeyInfoGet = "info-get";
    AppObject();
    virtual bool handleRequest(Request &req);
    virtual void handleEvent(Event &ev) {};
    virtual const JsonVariantConst descriptor() const {
      return json::emptyArray();
    };

    bool handleInfoRequest(Request &req);
    virtual JsonDocument *getInfo(RequestContext &ctx) {
      auto d = descriptor();
      if (d.isUnbound() || d.isNull() || d.size() == 0)
        return nullptr;
      auto doc = new JsonDocument();
      doc->set(d);
      return doc;
    }

    bool handleStateRequest(Request &req);
    virtual void setState(RequestContext &ctx) {}
    virtual JsonDocument *getState(RequestContext &ctx) {
      return nullptr;
    }

    bool handleConfigRequest(Request &req);
    virtual bool setConfig(RequestContext &ctx) {
      return false;
    }
    virtual JsonDocument *getConfig(RequestContext &ctx) {
      return nullptr;
    }

   private:
    // this flag is modified by config::Changed event
    // request, this flag, otherwise config manager will not
    // recognize config changes
    bool _configured = false;
    Subscription *_subscription;
    friend class config::Changed;
  };

  class EventStateChanged : public Event {
   public:
    EventStateChanged(const EventStateChanged &) = delete;
    AppObject *object() const {
      return _object;
    }
    JsonVariantConst state() const {
      return _state;
    }
    static void publish(AppObject *object) {
      EventStateChanged evt(object, JsonVariantConst());
      evt.Event::publish();
    }
    static void publish(AppObject *object, JsonVariantConst state) {
      EventStateChanged evt(object, state);
      evt.Event::publish();
    }
    static bool is(Event &ev, EventStateChanged **evsc = nullptr) {
      auto result = ev.is(Type);
      if (result && evsc)
        *evsc = (EventStateChanged *)&ev;
      return result;
    }
    static bool is(Event &ev, AppObject *obj) {
      return ev.is(Type) && ((EventStateChanged &)ev).object() == obj;
    }

   private:
    EventStateChanged(AppObject *object, JsonVariantConst state)
        : Event(Type), _object(object), _state(state) {}
    AppObject *_object;
    JsonVariantConst _state;
    constexpr static const char *Type = "state-changed";
  };

  class App : public AppObject {
   public:
    class Init {
     public:
      Init(const char *name = nullptr, const char *version = nullptr);
      ~Init();
      void requestInitLevels(uint8_t maxInitLevels);
      void setConfigStore(config::Store *store);
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
    JsonDocument *getState(RequestContext &ctx) override;
    bool setConfig(RequestContext &ctx) override;
    JsonDocument *getConfig(RequestContext &ctx) override;
    JsonDocument *getInfo(RequestContext &ctx) override;
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