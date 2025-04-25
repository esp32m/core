#include "esp32m/device.hpp"
#include "esp32m/events/request.hpp"

namespace esp32m {
  namespace integrations {
    namespace ha {

      JsonDocument *describeSensor(Sensor *sensor);

      class DescribeRequest : public Request {
       public:
        DescribeRequest(const char *target)
            : Request(Name, 0, target, json::null<JsonVariantConst>(),
                      nullptr) {}

        void respondImpl(const char *source, const JsonVariantConst data,
                         bool isError) override {
          if (data.isNull() || !data.size() || isError)
            return;
          std::string id = data["id"] | source;
          auto doc =
              new JsonDocument(); /* data.memoryUsage() + JSON_OBJECT_SIZE(1) */
          doc->set(data);
          doc->as<JsonObject>()["name"] = source;
          responses[id] = std::unique_ptr<JsonDocument>(doc);
        }

        static void autoRespond(Request &req, Sensor &sensor) {
          auto doc = ha::describeSensor(&sensor);
          req.respond(sensor.device()->name(), doc->as<JsonVariantConst>());
          delete doc;
        }

        constexpr static const char *Name = "ha-describe";

        std::map<std::string, std::unique_ptr<JsonDocument> > responses;
      };

      class StateRequest : public Request {
       public:
        StateRequest(const char *target, JsonVariantConst data)
            : Request(Name, 0, target, data, nullptr) {}

        void respondImpl(const char *source, const JsonVariantConst data,
                         bool isError) override {
          if (data.isNull() || !data.size() || isError)
            return;
          auto doc = new JsonDocument(); /* data.memoryUsage() */
          doc->set(data);
          response = std::unique_ptr<JsonDocument>(doc);
        }
        static void autoRespond(Request &req, Sensor &sensor) {
          auto id = req.data()["id"].as<const char *>();
          if (!id || sensor.uid() != id)
            return;
          auto value = sensor.get();
          JsonDocument *doc = new JsonDocument(
              /*JSON_OBJECT_SIZE(2) + value.memoryUsage()*/);
          auto root = doc->to<JsonObject>();
          auto state = root["state"].to<JsonObject>();
          state[sensor.id()] = value;
          req.respond(sensor.device()->name(), doc->as<JsonVariantConst>());
          delete doc;
        }
        constexpr static const char *Name = "ha-state";

        std::unique_ptr<JsonDocument> response;
      };

      class CommandRequest : public Request {
       public:
        CommandRequest(const char *target, JsonVariantConst data)
            : Request(Name, 0, target, data, nullptr) {}

        void respondImpl(const char *source, const JsonVariantConst data,
                         bool isError) override {}

        constexpr static const char *Name = "ha-command";
      };

    }  // namespace ha
  }    // namespace integrations
}  // namespace esp32m