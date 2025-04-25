#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/bus/modbus.hpp"
#include "esp32m/dev/em/em.hpp"
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {
    namespace sdm {

      enum class Model {
        Unknown,
        Sdm72d,
        Sdm120,
        Sdm120ct,
        Sdm220,
        Sdm230,
        Sdm630,
        Ddm18sd
      };

      enum Register {
        Voltage = 0x00,
        Current = 0x06,
        Power = 0x0c,
        ApparentPower = 0x12,
        ReactivePower = 0x18,
        PowerFactor = 0x1E,
        Angle = 0x24,
        AvgL2N = 0x2A,
        AvgLCurrent = 0x2E,
        SumLCurrent = 0x30,
        TotalSysPower = 0x34,
        TotalSysApparentPower = 0x38,
        TotalSysReactivePower = 0x3C,
        TotalSysPowerFactor = 0x3E,
        TotalSysAngle = 0x42,
        Frequency = 0x46,
        ImpActiveEnergy = 0x48,
        ExpActiveEnergy = 0x4A,
        ImpReactiveEnergy = 0x4C,
        ExpReactiveEnergy = 0x4E,
        VahSinceLastReset = 0x50,
        AhSinceLastReset = 0x52,
        TotalSysPowerDemand = 0x54,
        MaxTotalSysPowerDemand = 0x56,
        CurSysPositivePowerDemand = 0x58,
        MaxSysPositivePowerDemand = 0x5A,
        CurSysReversePowerDemand = 0x5C,
        MaxSysReversePowerDemand = 0x5E,
        TotalSysVaDemand = 0x64,
        MaxTotalSysVaDemand = 0x66,
        NeutralCurrentDemand = 0x68,
        MaxNeutralCurrent = 0x6A,
        L2LVoltage = 0xC8,
        AvgL2LVoltage = 0xCE,
        NeutralCurrent = 0xE0,
        PhaseLNVoltageThd = 0xEA,
        PhaseCurrentThd = 0xF0,
        AvgL2NVoltageThd = 0xF8,
        AvgLCurrentThd = 0xFA,
        TotalSysPowerFactorInv = 0xFE,
        PhaseCurrentDemand = 0x102,
        MaxPhaseCurrentDemand = 0x10C,
        L2LVoltageThd = 0x14E,
        AvgL2LVoltageThd = 0x154,
        TotalActiveEnergy = 0x156,
        TotalReactiveEnergy = 0x158,
        LImpActiveEnergy = 0x15A,
        LExpActiveEnergy = 0x160,
        LTotalActiveEnergy = 0x166,
        LImpReactiveEnergy = 0x16C,
        LExpReactiveEnergy = 0x172,
        LTotalReactiveEnergy = 0x178,
        CurResettableTotalActiveEnergy = 0x180,
        CurResettableTotalReactiveEnergy = 0x182,
        CurResettableImpEnergy = 0x184,
        CurResettableExpEnergy = 0x186,
        ImpPower = 0x500,
        ExpPower = 0x502
      };

      class Core : public virtual log::SimpleLoggable {
       public:
        Core() {}
        Core(const Core &) = delete;
        void init(Model model, uint8_t addr, const char *name) {
          _model = model;
          _addr = addr;
          const char *defName = "SDM";
          switch (model) {
            case Model::Sdm72d:
              defName = "SDM72D";
              break;
            case Model::Sdm120:
              defName = "SDM120";
              break;
            case Model::Sdm120ct:
              defName = "SDM120CT";
              break;
            case Model::Sdm220:
              defName = "SDM220";
              break;
            case Model::Sdm230:
              defName = "SDM230";
              break;
            case Model::Sdm630:
              defName = "SDM630";
              break;
            case Model::Ddm18sd:
              defName = "DDM12SD";
              break;
            default:
              break;
          }
          log::SimpleLoggable::init(name, defName);
        }
        bool isThreePhase() const {
          return _model == Model::Sdm630;
        }
        esp_err_t read(Register reg, float &res);
        esp_err_t read(Register reg, em::Indicator ind, bool triple = false);
        esp_err_t readAll();

        em::Data &data() {
          return _data;
        }

       protected:
        Model _model = Model::Unknown;
        uint8_t _addr = 0;
        em::Data _data;
      };

    }  // namespace sdm

    class Sdm : public virtual sdm::Core, public virtual Device {
     public:
      Sdm(sdm::Model model, uint8_t addr, const char *name);
      Sdm(const Sdm &) = delete;

     protected:
      bool pollSensors() override;
      bool initSensors() override;
      JsonDocument *getState(RequestContext &ctx) override;

     private:
      float _te = 0, _ee = 0, _ie = 0, _v = 0, _i = 0, _ap = 0, _rap = 0,
            _pf = 0, _f = 0;
      unsigned long _stamp;
    };

    Sdm *useSdm(sdm::Model model, uint8_t addr = 1, const char *name = nullptr);

  }  // namespace dev
}  // namespace esp32m