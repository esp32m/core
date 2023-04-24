#include "esp32m/dev/em/sdm.hpp"
#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {

  namespace dev {

    namespace sdm {

      esp_err_t Core::read(Register reg, float &res) {
        modbus::Master &mb = modbus::Master::instance();
        uint16_t hex[2] = {};
        // unfortunately, reading in bulk is not supported, at least not by
        // SDM230
        ESP_CHECK_RETURN(
            mb.request(_addr, modbus::Command::ReadInput, reg, 2, hex));
        ((uint16_t *)&res)[0] = hex[1];
        ((uint16_t *)&res)[1] = hex[0];
        return ESP_OK;
      }

      esp_err_t Core::read(Register reg, em::Indicator ind, bool triple) {
        float v[3];
        int c = triple ? 3 : 1;
        for (int i = 0; i < c; i++)
          ESP_CHECK_RETURN(read((Register)(reg + i), v[i]));
        _data.set(ind, v, triple);
        return ESP_OK;
      }

      esp_err_t Core::readAll() {
        bool three = isThreePhase();
        if (_model == Model::Sdm630 || _model == Model::Sdm230 ||
            _model == Model::Sdm220 || _model == Model::Sdm120ct ||
            _model == Model::Sdm120) {
          ESP_CHECK_RETURN(
              read(Register::Voltage, em::Indicator::Voltage, three));
          ESP_CHECK_RETURN(
              read(Register::Current, em::Indicator::Current, three));
          ESP_CHECK_RETURN(read(Register::Power, em::Indicator::Power, three));
          ESP_CHECK_RETURN(read(Register::ApparentPower,
                                em::Indicator::ApparentPower, three));
          ESP_CHECK_RETURN(read(Register::ReactivePower,
                                em::Indicator::ReactivePower, three));
          ESP_CHECK_RETURN(
              read(Register::PowerFactor, em::Indicator::PowerFactor, three));
          if (_model != Model::Sdm120)
            ESP_CHECK_RETURN(
                read(Register::Angle, em::Indicator::Angle, three));
          ESP_CHECK_RETURN(read(Register::Frequency, em::Indicator::Frequency));
          ESP_CHECK_RETURN(
              read(Register::ImpActiveEnergy, em::Indicator::ImpActiveEnergy));
          ESP_CHECK_RETURN(
              read(Register::ExpActiveEnergy, em::Indicator::ExpActiveEnergy));
          ESP_CHECK_RETURN(read(Register::ImpReactiveEnergy,
                                em::Indicator::ImpReactiveEnergy));
          ESP_CHECK_RETURN(read(Register::ExpReactiveEnergy,
                                em::Indicator::ExpReactiveEnergy));
          ESP_CHECK_RETURN(read(Register::TotalActiveEnergy,
                                em::Indicator::TotalActiveEnergy));
          ESP_CHECK_RETURN(read(Register::TotalReactiveEnergy,
                                em::Indicator::TotalReactiveEnergy));
        }
        return ESP_OK;
      }

    }  // namespace sdm

    Sdm::Sdm(sdm::Model model, uint8_t addr, const char *name)
    {
      Device::init(Flags::HasSensors);
      sdm::Core::init(model, addr, name);
    }

    bool Sdm::initSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      return mb.isRunning();
    }

    DynamicJsonDocument *Sdm::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(
          JSON_ARRAY_SIZE(3) + JSON_OBJECT_SIZE(1) + _data.jsonSize());
      JsonArray arr = doc->to<JsonArray>();
      arr.add(millis() - _stamp);
      auto obj = arr.createNestedObject();
      _data.toJson(obj);
      return doc;
    }

    bool Sdm::pollSensors() {
      modbus::Master &mb = modbus::Master::instance();
      if (!mb.isRunning())
        return false;
      ESP_CHECK_RETURN_BOOL(readAll());
      _data.sensors(this);
      _stamp = millis();
      return true;
    }

    Sdm *useSdm(sdm::Model model, uint8_t addr, const char *name) {
      return new Sdm(model, addr, name);
    }

  }  // namespace dev
}  // namespace esp32m