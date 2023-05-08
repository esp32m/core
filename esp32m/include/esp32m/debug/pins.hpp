#pragma once

#include "esp32m/app.hpp"

namespace esp32m {

  namespace debug {

    using namespace io;

    class Pins : public AppObject {
     public:
      Pins(const Pins &) = delete;
      const char *name() const override {
        return "debug-pins";
      }

      static Pins &instance() {
        static Pins i;
        return i;
      }

     protected:
      bool handleRequest(Request &req) override {
        if (AppObject::handleRequest(req))
          return true;
        if (req.is("enum")) {
          auto providers = pins::getProviders();
          size_t size = 0;
          for (auto const &[name, pins] : providers) {
            size += JSON_OBJECT_SIZE(1);
            for (int i = 0; i < pins->count(); i++) {
              auto pin = pins->pin(i);
              if (pin)
                size += JSON_ARRAY_SIZE(1);
            }
          }
          auto doc = new DynamicJsonDocument(size);
          auto root = doc->to<JsonObject>();
          for (auto const &[name, pins] : providers) {
            auto p = root.createNestedArray(pins->name());
            for (int i = 0; i < pins->count(); i++) {
              auto pin = pins->pin(i);
              if (pin)
                p.add(pin->id());
            }
          }
          req.respond(root, false);
          delete doc;
          return true;
        }
        if (req.is("feature")) {
          auto data = req.data().as<JsonArrayConst>();
          auto pin = toPin(data[0]);
          auto feature = toFeatureType(data[1]);
          if (pin && feature != pin::Type::Invalid) {
            pin->feature((pin::Type)feature);
          }
          req.respond();
          return true;
        }
        return false;
      }
      void setState(const JsonVariantConst args,
                    DynamicJsonDocument **result) override {
        auto pin = toPin(args["pin"]);
        auto type = toFeatureType(args["feature"]);
        auto state = args["state"].as<JsonObjectConst>();
        if (pin && type != pin::Type::Invalid &&
            pin->featureStatus(type) == pin::FeatureStatus::Enabled && state) {
          JsonVariantConst v;
          switch (type) {
            case pin::Type::Digital: {
              auto digital = pin->digital();
              if (digital) {
                v = state["high"];
                if (!v.isNull())
                  digital->write(v);
                v = state["mode"];
                if (!v.isNull() && v.is<int>())
                  digital->setDirection((gpio_mode_t)v.as<int>());
                v = state["pull"];
                if (!v.isNull() && v.is<int>())
                  digital->setPull((gpio_pull_mode_t)v.as<int>());
              }
            } break;
            case pin::Type::PWM: {
              auto pwm = pin->pwm();
              if (pwm) {
                v = state["freq"];
                if (v.is<int>())
                  pwm->setFreq(v);
                v = state["duty"];
                if (v.is<float>())
                  pwm->setDuty(v);
                v = state["enabled"];
                if (v.is<bool>())
                  pwm->enable(v);
              }
            } break;

            default:
              break;
          }
        }
      }
      DynamicJsonDocument *getState(const JsonVariantConst args) override {
        auto pin = toPin(args["pin"]);
        auto type = toFeatureType(args["feature"]);
        if (!pin || (type != pin::Type::Invalid &&
                     pin->featureStatus(type) != pin::FeatureStatus::Enabled))
          type = pin::Type::Invalid;
        size_t size = JSON_OBJECT_SIZE(1) +
                      JSON_ARRAY_SIZE((int)pin::Type::MAX)  // features
            ;
        if (type != pin::Type::Invalid) {
          size += JSON_OBJECT_SIZE(1);  // state
          switch (type) {
            case pin::Type::Digital:
              size += JSON_OBJECT_SIZE(3);
              break;
            case pin::Type::PWM:
              size += JSON_OBJECT_SIZE(3);
              break;
            default:
              break;
          }
        }
        auto doc = new DynamicJsonDocument(size);
        auto root = doc->to<JsonObject>();
        if (pin) {
          auto features = root.createNestedArray("features");
          for (int t = 0; t < (int)pin::Type::MAX; t++)
            features.add((int)pin->featureStatus((pin::Type)t));
          if (type != pin::Type::Invalid) {
            auto state = root.createNestedObject("state");
            switch (type) {
              case pin::Type::Digital: {
                auto digital = pin->digital();
                if (digital) {
                  bool high = false;
                  digital->read(high);
                  state["high"] = high;
                }
              } break;
              case pin::Type::PWM: {
                auto pwm = pin->pwm();
                if (pwm) {
                  state["enabled"] = pwm->isEnabled();
                  state["freq"] = pwm->getFreq();
                  state["duty"] = pwm->getDuty();
                }
              } break;

              default:
                break;
            }
          }
        }
        return doc;
      }

     private:
      IPin *toPin(JsonArrayConst key) {
        if (key && key.size() == 2)
          return pins::pin(key[0].as<const char *>(), key[1].as<int>());
        return nullptr;
      }
      pin::Type toFeatureType(JsonVariantConst v) {
        if (v.is<int>()) {
          auto feature = v.as<int>();
          if (feature >= 0 && feature < (int)pin::Type::MAX)
            return (pin::Type)feature;
        }
        return pin::Type::Invalid;
      }
      Pins() {}
    };

    static Pins *usePins() {
      return &Pins::instance();
    }

  }  // namespace debug
}  // namespace esp32m