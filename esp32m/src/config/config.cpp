#include "esp32m/config.hpp"
#include "esp32m/config/configurable.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/json.hpp"

namespace esp32m {
  const char *Config::KeyConfigGet = "config-get";
  const char *Config::KeyConfigSet = "config-set";

  class ConfigRequest : public Request {
   public:
    ConfigRequest()
        : Request(Config::KeyConfigGet, 0, nullptr,
                  json::null<JsonVariantConst>(), nullptr) {}

    void respondImpl(const char *source, const JsonVariantConst data,
                     bool isError) override {
      if (data.isNull() || !data.size())
        return;
      auto doc = new DynamicJsonDocument(data.memoryUsage());
      doc->set(data);
      _responses.add(source, doc);
      json::checkEqual(data, doc->as<JsonVariantConst>());
    }

    DynamicJsonDocument *merge() {
      return _responses.concat();
    }

   private:
    json::ConcatToObject _responses;
  };

  class ConfigApply : public Request {
   public:
    ConfigApply(const JsonVariantConst data)
        : Request(Config::KeyConfigSet, 0, nullptr, data, nullptr) {}
    void respondImpl(const char *source, const JsonVariantConst data,
                     bool isError) override {}
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
      _store->write(doc->as<JsonVariantConst>());
      delete doc;
    }
  }

  void Config::load() {
    if (!_store)
      return;
    DynamicJsonDocument *doc;
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

  DynamicJsonDocument *Config::read() {
    if (!_store)
      return nullptr;
    DynamicJsonDocument *doc;
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