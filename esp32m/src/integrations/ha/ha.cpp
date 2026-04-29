#include "esp32m/integrations/ha/ha.hpp"

namespace esp32m {

  namespace integrations {
    namespace ha {

      const char* stateClasses[] = {"measurement", "total", "total_increasing"};

      JsonDocument* describeComponent(Component* component) {
        auto uid = component->uid();
        auto id = component->id();
        auto title = component->title();
        auto precision = component->precision;
        const char* unit = nullptr;
        const char* stateClassName = nullptr;
        Sensor* sensor =
            component->isSensor() ? static_cast<Sensor*>(component) : nullptr;
        if (sensor) {
          auto stateClass = (int)sensor->stateClass;
          if (stateClass > 0 && stateClass <= 3)
            stateClass--;
          else
            stateClass = -1;
          if (stateClass >= 0)
            stateClassName = stateClasses[stateClass];

          unit = sensor->unit;
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
            else if (sensor->is("power"))
              unit = "W";
            else if (sensor->is("apparent_power"))
              unit = "VA";
            else if (sensor->is("reactive_power"))
              unit = "var";
            else if (sensor->is("power_factor"))
              unit = "%";
            else if (sensor->is("frequency"))
              unit = "Hz";
          }
        }
        // auto group = component->group;

        JsonDocument* doc = new JsonDocument();
        auto root = doc->to<JsonObject>();
        root["id"] = uid;
        root["name"] = component->device()->name();
        root["componentId"] = id;
        root["component"] = component->component();
        root["has_state"] = component->hasState(  );
        root["accepts_commands"] = component->acceptsCommands(  );
        if (component->isComponent(ComponentType::Cover)) {
          auto stateTopicNames = root["stateTopicNames"].to<JsonArray>();
          stateTopicNames.add("state_topic");
          stateTopicNames.add("position_topic");
        }
        auto config = root["config"].to<JsonObject>();
        if (title)
          config["name"] = title;
        if (component->type() && strlen(component->type()) > 0)
          config["device_class"] = component->type();
        if (unit)
          config["unit_of_measurement"] = unit;
        if (precision >= 0)
          config["suggested_display_precision"] = precision;
        if (stateClassName)
          config["state_class"] = stateClassName;
        if (component->isComponent(ComponentType::BinarySensor) ||
          component->isComponent(ComponentType::Switch)) {
          config["value_template"] =
              string_printf("{{ 'ON' if value_json['%s'] else 'OFF' }}", id);
        } else if (component->isComponent(ComponentType::Cover)) {
          config["value_template"] = string_printf(
            "{{ value_json['%s'].get('state') if value_json['%s'] is mapping else none }}",
            id, id);
          config["position_template"] = string_printf(
            "{{ value_json['%s'].get('position') if value_json['%s'] is mapping else none }}",
            id, id);
        } else
          config["value_template"] = string_printf("{{value_json['%s']}}", id);
        if (component->isComponent(ComponentType::Select)) {
          config["platform"] = "select";
          auto options=config["options"].to<JsonArray>();
          for (auto& opt: static_cast<Select*>(component)->options())
            options.add(opt);
        } else if (component->isComponent(ComponentType::Cover)) {
          config["platform"] = "cover";
        }
        return doc;
      }
    }  // namespace ha
  }  // namespace integrations
}  // namespace esp32m