#pragma once

#include <assert.h>
#include <functional>
#include <mutex>
#include <vector>

namespace esp32m {

  /**
   * @brief Base class for events. May be subclassed to add custom properties
   * and logic to custom events
   */
  class Event {
   public:
    /**
     * @brief Constructs new event with the given type
     */
    Event(const char *type) : _type(type) {
      assert(type);
    }
    /**
     * @returns Event type
     */
    const char *type() const {
      return _type;
    }
    /**
     * @brief Checks if this event is of the given type
     * @return @c true if the event is of this type, @c false otherwise
     */
    bool is(const char *type) const;
    /**
     *  @brief Helper method to publish this event using @c EventMaanger
     * singleton
     */
    virtual void publish();

   private:
    const char *_type;
  };

  /**
   * @brief Holds subscription details
   */
  struct Subscription {
   public:
    /**
     * @brief Callback function to be called when event is dispatched to this
     * subscriber
     */
    typedef std::function<void(Event &event)> Callback;
    Subscription(const Subscription &) = delete;
    /**
     * @brief delete this subscription to un-subscribe
     */
    ~Subscription();

   private:
    Callback _cb;
    uint8_t _refcnt;
    Subscription(Callback cb) : _cb(cb), _refcnt(0) {}
    void ref() {
      _refcnt++;
    }
    void unref() {
      _refcnt--;
    }
    friend class EventManager;
  };

  /**
   * @brief Event manager
   */
  class EventManager {
   public:
    EventManager(const EventManager &) = delete;
    /**
     * @brief Publish the given event
     * @param event Event to publish
     */
    void publish(Event &event);
    void publishBackwards(Event &event);
    /**
     * @brief Subscribe for events
     * @param cb Callback function to be invoked when any event is fired
     */
    const Subscription *subscribe(Subscription::Callback cb);

    static EventManager &instance();

   private:
    EventManager() {}
    std::vector<Subscription *> _subscriptions;
    std::mutex _mutex;
    void unsubscribe(const Subscription *sub);
    friend class Subscription;
  };

}  // namespace esp32m
