#include <string.h>

#include "esp32m/base.hpp"
#include "esp32m/events.hpp"

namespace esp32m {

  bool Event::is(const char *type) const {
    return type && !strcmp(_type, type);
  }

  void Event::publish() {
    EventManager::instance().publish(*this);
  }

  Subscription::~Subscription() {
    EventManager::instance().unsubscribe(this);
  }

  void EventManager::publish(Event &event) {
    for (auto i = 0; i < _subscriptions.size(); i++) {
      Subscription *sub;
      {
        std::lock_guard<std::mutex> guard(_mutex);
        sub = _subscriptions[i];
        if (!sub)
          continue;
        sub->ref();
      }
      sub->_cb(event);
      {
        std::lock_guard<std::mutex> guard(_mutex);
        sub->unref();
      }
    }
  }

  void EventManager::publishBackwards(Event &event) {
    for (int i = _subscriptions.size() - 1; i >= 0; i--) {
      Subscription *sub;
      {
        std::lock_guard<std::mutex> guard(_mutex);
        sub = _subscriptions[i];
        if (!sub)
          continue;
        sub->ref();
      }
      sub->_cb(event);
      {
        std::lock_guard<std::mutex> guard(_mutex);
        sub->unref();
      }
    }
  }

  const Subscription *EventManager::subscribe(Subscription::Callback cb) {
    const auto result = new Subscription(cb);
    std::lock_guard<std::mutex> guard(_mutex);
    for (auto it = _subscriptions.begin(); it != _subscriptions.end(); ++it)
      if (*it == nullptr)  // fill empty spots that may have appeared due to
                           // unsubscribe
      {
        *it = result;
        return result;
      }
    _subscriptions.push_back(result);
    return result;
  }

  void EventManager::unsubscribe(const Subscription *sub) {
    std::lock_guard<std::mutex> guard(_mutex);
    while (sub->_refcnt) delay(1);
    for (auto it = _subscriptions.begin(); it != _subscriptions.end(); ++it)
      if (*it == sub)
        *it =
            nullptr;  // we don't actually remove the element for thread safety
  }

  EventManager &EventManager::instance() {
    static EventManager i;
    return i;
  }

}  // namespace esp32m