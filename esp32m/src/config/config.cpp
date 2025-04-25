#include "esp32m/config/config.hpp"
#include "esp32m/app.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/json.hpp"

namespace esp32m {
  const char *Config::KeyConfigGet = "config-get";
  const char *Config::KeyConfigSet = "config-set";

  namespace config {

    void Changed::publish(AppObject *configurable, bool saveNow) {
      Changed evt(configurable, saveNow);
      configurable->_configured = true;
      evt.Event::publish();
    }

  }  // namespace config

  class ConfigRequest : public Request {
   public:
    ConfigRequest()
        : Request(Config::KeyConfigGet, 0, nullptr,
                  json::null<JsonVariantConst>(), nullptr) {}

    void respondImpl(const char *source, const JsonVariantConst data,
                     bool isError) override {
      if (data.isNull() || !data.size())
        return;
      auto doc = new JsonDocument(); /* data.memoryUsage() */
      doc->set(data);
      _responses.add(source, doc);
      json::checkEqual(data, doc->as<JsonVariantConst>());
    }

    JsonDocument *merge() {
      return _responses.concat();
    }

   private:
    json::ConcatToObject _responses;
  };

  void Config::save() {
    if (!_store)
      return;
    ConfigRequest ev;
    ev.publish();
    auto doc = ev.merge();
    if (doc) {
      json::check(this, doc, "save()");
      std::lock_guard<std::mutex> guard(_mutex);
      // _store->write(doc->as<JsonVariantConst>());
      _store->write(*doc);
      delete doc;
    }
  }

  void Config::load() {
    if (!_store)
      return;
    JsonDocument *doc;
    {
      std::lock_guard<std::mutex> guard(_mutex);
      doc = _store->read();
    }
    if (!doc)
      return;
    json::check(this, doc, "load()");
    ConfigApply ev(doc->as<JsonVariantConst>());
    ev.publish();
    delete doc;
  }

  JsonDocument *Config::read() {
    if (!_store)
      return nullptr;
    JsonDocument *doc;
    {
      std::lock_guard<std::mutex> guard(_mutex);
      doc = _store->read();
    }
    if (doc)
      json::check(this, doc, "load()");
    return doc;
  }

  void Config::reset() {
    if (!_store)
      return;
    {
      std::lock_guard<std::mutex> guard(_mutex);
      _store->reset();
    }
    ConfigApply ev(json::null<JsonVariantConst>());
    ev.publish();
  }

}  // namespace esp32m