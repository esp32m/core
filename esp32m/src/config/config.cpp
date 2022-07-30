#include "esp32m/config.hpp"
#include "esp32m/config/changed.hpp"
#include "esp32m/config/configurable.hpp"
#include "esp32m/events/request.hpp"
#include "esp32m/json.hpp"

namespace esp32m {
  const char *Config::KeyConfigGet = "config-get";
  const char *Config::KeyConfigSet = "config-set";
  const char *EventConfigChanged::NAME = "config-changed";
  namespace config {
    int _internalRequest = 0;
    bool isInternalRequest() {
      return _internalRequest > 0;
    }

  }  // namespace config
  class ConfigRequest : public Request {
   public:
    ConfigRequest()
        : Request(Config::KeyConfigGet, 0, nullptr,
                  json::null<JsonVariantConst>()) {}

    void respondImpl(const char *source, const JsonVariantConst data,
                     bool isError) override {
      if (data.isNull() || !data.size())
        return;
      auto doc = new DynamicJsonDocument(data.memoryUsage());
      doc->set(data);
      _responses.add(source, doc);

      /*      auto mu = data.memoryUsage();
            mu += JSON_OBJECT_SIZE(1);
            auto doc = new DynamicJsonDocument(mu);
            doc->to<JsonObject>()[source] = data;
            _documents.push_back(std::unique_ptr<DynamicJsonDocument>(doc));*/
    }

    DynamicJsonDocument *merge() {
      return _responses.concat();
      /*size_t mu = 0;
      for (auto &d : _documents) mu += d->memoryUsage();
      auto doc = new DynamicJsonDocument(mu);
      for (auto &d : _documents)
        for (auto kvp : d->as<JsonObjectConst>())
          (*doc)[kvp.key()] = kvp.value();
      return doc;*/
    }

   private:
    // std::vector<std::unique_ptr<DynamicJsonDocument> > _documents;
    json::ConcatToObject _responses;
  };

  class ConfigApply : public Request {
   public:
    ConfigApply(const JsonVariantConst data)
        : Request(Config::KeyConfigSet, 0, nullptr, data) {}
    void respondImpl(const char *source, const JsonVariantConst data,
                     bool isError) override {}
  };

  void Config::save() {
    if (!_store)
      return;
    ConfigRequest ev;
    config::_internalRequest++;
    ev.publish();
    config::_internalRequest--;
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