#include "esp32m/ha/ha.hpp"

namespace esp32m {

  namespace ha {

    const char *stateClasses[] = {"measurement", "total", "total_increasing"};

    DynamicJsonDocument *describeSensor(Sensor *sensor) {
      auto uid = sensor->uid();
      auto id = sensor->id();
      auto name = sensor->name;
      auto precision = sensor->precision;
      auto stateClass = (int)sensor->stateClass;
      if (stateClass > 0 && stateClass <= 3)
        stateClass--;
      else
        stateClass = -1;
      auto unit = sensor->unit;
      if (!unit) {
        if (sensor->is("temperature"))
          unit = "°C";
        else if (sensor->is("humidity"))
          unit = "%";
        else if (sensor->is("signal_strength"))
          unit = "dBm";
        else if (sensor->is("energy"))
          unit = "kWh";
        else if (sensor->is("voltage"))
          unit = "V";
        else if (sensor->is("current"))
          unit = "A";
        else if (sensor->is("apparent_power"))
          unit = "VA";
        else if (sensor->is("reactive_power"))
          unit = "var";
        else if (sensor->is("power_factor"))
          unit = "%";
        else if (sensor->is("frequency"))
          unit = "Hz";
      }
      auto group = sensor->group;

      DynamicJsonDocument *doc = new DynamicJsonDocument(
          JSON_OBJECT_SIZE(8 + (group > 0 ? 1 : 0) + (unit ? 1 : 0) +
                           (precision >= 0 ? 1 : 0) + (name ? 1 : 0)) +
          (stateClass >= 0 ? 1 : 0) + JSON_STRING_SIZE(uid.size()) +
          JSON_STRING_SIZE(strlen(id) + 15)  // {{value_json.%id%}}
      );
      auto root = doc->to<JsonObject>();
      root["id"] = uid;
      root["name"] = sensor->device()->name();
      root["componentId"] = id;
      root["component"] = "sensor";
      if (group > 0)
        root["group"] = group;
      root["state_class"] = "measurement";
      auto config = root.createNestedObject("config");
      if (name)
        config["name"] = name;
      config["device_class"] = sensor->type();
      if (unit)
        config["unit_of_measurement"] = unit;
      if (precision >= 0)
        config["suggested_display_precision"] = precision;
      if (stateClass >= 0)
        config["state_class"] = stateClasses[stateClass];
      config["value_template"] = string_printf("{{value_json.%s}}", id);
      return doc;
    }
  }  // namespace ha
}  // namespace esp32m