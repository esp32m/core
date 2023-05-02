#include "esp32m/events/request.hpp"

namespace esp32m {
  namespace ha {

    class Descriptor {
     public:
      bool acceptsCommands = false;
      std::vector<const char *> stateTopicNames;
    };

    class DescribeRequest : public Request {
     public:
      DescribeRequest(const char *target)
          : Request(Name, 0, target, json::null<JsonVariantConst>(), nullptr) {}

      void respondImpl(const char *source, const JsonVariantConst data,
                       bool isError) override {
        if (data.isNull() || !data.size() || isError)
          return;
        std::string id = data["id"] | source;
        auto doc =
            new DynamicJsonDocument(data.memoryUsage() + JSON_OBJECT_SIZE(1));
        doc->set(data);
        doc->as<JsonObject>()["name"] = source;
        responses[id] = std::unique_ptr<DynamicJsonDocument>(doc);
      }

      static void setAcceptsCommands(JsonObject data) {
        data["acceptsCommands"] = true;
      }
      static bool getAcceptsCommands(const JsonObjectConst data) {
        return data["acceptsCommands"].as<bool>();
      }

      constexpr static const char *Name = "ha-describe";

      std::map<std::string, std::unique_ptr<DynamicJsonDocument> > responses;
    };

    class StateRequest : public Request {
     public:
      StateRequest(const char *target, JsonVariantConst data)
          : Request(Name, 0, target, data, nullptr) {}

      void respondImpl(const char *source, const JsonVariantConst data,
                       bool isError) override {
        if (data.isNull() || !data.size() || isError)
          return;
        auto doc = new DynamicJsonDocument(data.memoryUsage());
        doc->set(data);
        response = std::unique_ptr<DynamicJsonDocument>(doc);
      }

      constexpr static const char *Name = "ha-state";

      std::unique_ptr<DynamicJsonDocument> response;
    };

    class CommandRequest : public Request {
     public:
      CommandRequest(const char *target, JsonVariantConst data)
          : Request(Name, 0, target, data, nullptr) {}

      void respondImpl(const char *source, const JsonVariantConst data,
                       bool isError) override {
      }

      constexpr static const char *Name = "ha-command";
    };

  }  // namespace ha
}  // namespace esp32m