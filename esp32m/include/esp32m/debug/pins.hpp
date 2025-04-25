#pragma once

#include "esp32m/app.hpp"
#include "esp32m/io/pins.hpp"

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
          /*size_t size = 0;
          for (auto const &[name, pins] : providers) {
            size += JSON_OBJECT_SIZE(1);
            for (int i = 0; i < pins->count(); i++) {
              auto pin = pins->pin(i);
              if (pin)
                size += JSON_ARRAY_SIZE(1);
            }
          }*/
          auto doc = new JsonDocument(); /* size */
          auto root = doc->to<JsonObject>();
          for (auto const &[name, pins] : providers) {
            auto p = root[pins->name()].to<JsonArray>();
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
      void setState(RequestContext& ctx) override {
        auto args=ctx.data.as<JsonObjectConst>();
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
                  digital->setMode((pin::Mode)v.as<int>());
              }
            } break;
            case pin::Type::ADC: {
              auto adc = pin->adc();
              if (adc) {
                v = state["atten"];
                if (v.is<int>())
                  adc->setAtten((adc_atten_t)v.as<int>());
                v = state["width"];
                if (v.is<int>())
                  adc->setWidth((adc_bitwidth_t)v.as<int>());
              }
            } break;
            case pin::Type::DAC: {
              auto dac = pin->dac();
              if (dac) {
                v = state["value"];
                if (v.is<float>())
                  dac->write(v.as<float>());
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
            case pin::Type::Pcnt: {
              auto pcnt = pin->pcnt();
              if (pcnt) {
                auto pea = state["pea"];
                auto nea = state["nea"];
                if (pea.is<int>() && nea.is<int>())
                  pcnt->setEdgeAction((pcnt_channel_edge_action_t)pea,
                                      (pcnt_channel_edge_action_t)nea);
                auto hla = state["hla"];
                auto lla = state["lla"];
                if (hla.is<int>() && lla.is<int>())
                  pcnt->setLevelAction((pcnt_channel_level_action_t)hla,
                                       (pcnt_channel_level_action_t)lla);
                v = state["gns"];
                if (v.is<int>())
                  pcnt->setFilter((uint32_t)v);

                v = state["enabled"];
                if (v.is<bool>()) {
                  pcnt->getSampler();
                  pcnt->enable(v);
                }
              }
            } break;

            default:
              break;
          }
        }
      }
      JsonDocument *getState(RequestContext &ctx) override {
        auto pin = toPin(ctx.data["pin"]);
        auto type = toFeatureType(ctx.data["feature"]);
        if (!pin || (type != pin::Type::Invalid &&
                     pin->featureStatus(type) != pin::FeatureStatus::Enabled))
          type = pin::Type::Invalid;
        /*size_t size = JSON_OBJECT_SIZE(2) +                 // features, flags
                      JSON_ARRAY_SIZE((int)pin::Type::MAX)  // features
            ;
        if (type != pin::Type::Invalid) {
          size += JSON_OBJECT_SIZE(1);  // state
          switch (type) {
            case pin::Type::Digital:
              size += JSON_OBJECT_SIZE(2);
              break;
            case pin::Type::ADC:
              size += JSON_OBJECT_SIZE(4);
              break;
            case pin::Type::DAC:
              size += JSON_OBJECT_SIZE(1);
              break;
            case pin::Type::PWM:
              size += JSON_OBJECT_SIZE(3);
              break;
            case pin::Type::Pcnt:
              size += JSON_OBJECT_SIZE(8);
              break;
            default:
              break;
          }
        }*/
        auto doc = new JsonDocument(); /* size */
        auto root = doc->to<JsonObject>();
        if (pin) {
          root["flags"] = (int)pin->flags();
          auto features = root["features"].to<JsonArray>();
          for (int t = 0; t < (int)pin::Type::MAX; t++)
            features.add((int)pin->featureStatus((pin::Type)t));
          if (type != pin::Type::Invalid) {
            auto state = root["state"].to<JsonObject>();
            switch (type) {
              case pin::Type::Digital: {
                auto digital = pin->digital();
                if (digital) {
                  bool high = false;
                  digital->read(high);
                  state["high"] = high;
                  state["mode"] = (int)digital->getMode();
                }
              } break;
              case pin::Type::ADC: {
                auto adc = pin->adc();
                if (adc) {
                  state["atten"] = (int)adc->getAtten();
                  state["width"] = (int)adc->getWidth();
                  float value;
                  uint32_t mv;
                  if (adc->read(value, &mv) == ESP_OK) {
                    state["value"] = value;
                    state["mv"] = mv;
                  }
                }
              } break;
              case pin::Type::DAC: {
                auto dac = pin->dac();
                if (dac) {
                  state["value"] = dac->getValue();
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
              case pin::Type::Pcnt: {
                auto pcnt = pin->pcnt();
                if (pcnt) {
                  state["enabled"] = pcnt->isEnabled();
                  int v;
                  if (pcnt->read(v) == ESP_OK)
                    state["value"] = v;
                  auto sampler = pcnt->getSampler();
                  if (sampler) {
                    state["freq"] = sampler->getFreq();
                  }
                  pcnt_channel_edge_action_t pea, nea;
                  if (pcnt->getEdgeAction(pea, nea) == ESP_OK) {
                    state["pea"] = pea;
                    state["nea"] = nea;
                  }
                  pcnt_channel_level_action_t hla, lla;
                  if (pcnt->getLevelAction(hla, lla) == ESP_OK) {
                    state["hla"] = hla;
                    state["lla"] = lla;
                  }
                  uint32_t gns;
                  if (pcnt->getFilter(gns) == ESP_OK) {
                    state["gns"] = gns;
                  }
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