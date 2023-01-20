#pragma once

#include "esp32m/events.hpp"

#include <map>
#include <mutex>

namespace esp32m {

  class EventPropChanged : public Event {
   public:
    static bool is(Event &ev, const char *name, const char *key = nullptr) {
      if (!ev.is(NAME))
        return false;
      auto &e = (EventPropChanged &)ev;
      if (name && e._name && strcmp(name, e._name))
        return false;
      if (key && e._key && strcmp(key, e._key))
        return false;
      return true;
    }
    static void publish(const char *name, const char *key, std::string prev,
                        std::string next) {
      EventPropChanged ev(name, key, prev, next);
      EventManager::instance().publish(ev);
    }
    const char *name() const {
      return _name;
    }
    const char *key() const {
      return _key;
    }
    std::string prev() const {
      return _prev;
    }
    std::string next() const {
      return _next;
    }

   private:
    EventPropChanged(const char *name, const char *key, std::string prev,
                     std::string next)
        : Event(NAME), _name(name), _key(key), _prev(prev), _next(next) {}
    const char *_name;
    const char *_key;
    std::string _prev, _next;
    static const char *NAME;
  };

  class Props {
   public:
    Props(const char *name) : _name(name) {}
    void set(const char *key, std::string value) {
      std::string prev;
      {
        std::lock_guard guard(_mutex);
        prev = _map[key];
        if (prev == value)
          return;
        _map[key] = value;
      }
      EventPropChanged::publish(_name, key, prev, value);
    }
    std::string get(const char *key) {
      std::lock_guard guard(_mutex);
      return _map[key];
    }
    std::map<std::string, std::string> get() {
      std::map<std::string, std::string> result;
      {
        std::lock_guard guard(_mutex);
        result = _map;
      }
      return result;
    }

   private:
    const char *_name;
    std::mutex _mutex;
    std::map<std::string, std::string> _map;
  };
}  // namespace esp32m