#include "esp32m/events/request.hpp"

namespace esp32m {
  namespace ha {

    class DescribeRequest : public Request {
     public:
      DescribeRequest(const char *target)
          : Request(Name, 0, target, json::null<JsonVariantConst>(), nullptr) {}

      void respondImpl(const char *source, const JsonVariantConst data,
                       bool isError) override {
        if (data.isNull() || !data.size() || isError)
          return;
        auto doc = new DynamicJsonDocument(data.memoryUsage());
        doc->set(data);
        responses[source] = std::unique_ptr<DynamicJsonDocument>(doc);
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

  }  // namespace ha
}  // namespace esp32m