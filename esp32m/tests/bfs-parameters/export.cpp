#include <ArduinoJson.h>
#include <iostream>
#include "esp32m/dev/bfs/parameters.hpp"

int main() {
  JsonDocument doc;
  auto catalog = doc.to<JsonArray>();
  for (const auto& parameter : esp32m::dev::bfs::Parameters) {
    auto out = catalog.add<JsonObject>();
    out["address"] = parameter.address;
    out["id"] = parameter.id;
    out["name"] = parameter.name;
    out["group"] = parameter.group;
    out["writable"] = parameter.writable;
    if (parameter.unit && *parameter.unit) out["unit"] = parameter.unit;
    if (parameter.scale > 0) {
      out["scale"] = parameter.scale;
      out["min"] = parameter.min * parameter.scale;
      out["max"] = parameter.max * parameter.scale;
      out["step"] = parameter.scale;
    }
    if (parameter.reason && *parameter.reason) out["reason"] = parameter.reason;
    if (parameter.inferred) out["inferred"] = true;
    if (parameter.signedValue) out["signed"] = true;
    if (parameter.optionCount) {
      auto options = out["options"].to<JsonArray>();
      for (size_t i = 0; i < parameter.optionCount; ++i) {
        auto option = options.add<JsonObject>();
        option["value"] = parameter.options[i].value;
        option["label"] = parameter.options[i].label;
      }
    }
  }
  serializeJson(doc, std::cout);
}
