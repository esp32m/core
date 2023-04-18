#pragma once

#include "esp32m/device.hpp"
#include "esp32m/events/response.hpp"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <map>

namespace esp32m {
  namespace opentherm {

    const int FrameSizeBits = 32 + 2;

    enum class MessageType : uint8_t {
      /*  Master to Slave */
      ReadData = 0x00,
      WriteData = 0x01,
      InvalidData = 0x02,
      Reserved = 0x03,
      /* Slave to Master */
      ReadAck = 0x04,
      WriteAck = 0x05,
      DataInvalid = 0x06,
      UnknownDataId = 0x07
    };

    enum class DataId : uint8_t {
      Status,  // flag8 / flag8  Master and Slave Status flags.
      TSet,  // f8.8  Control setpoint  ie CH  water temperature setpoint (°C)
      MConfigMMemberIDcode,  // flag8 / u8  Master Configuration Flags /  Master
                             // MemberID Code
      SConfigSMemberIDcode,  // flag8 / u8  Slave Configuration Flags /  Slave
                             // MemberID Code
      Command,               // u8 / u8  Remote Command
      ASFflags,  // / OEM-fault-code  flag8 / u8  Application-specific fault
                 // flags and OEM fault code
      RBPflags,  // flag8 / flag8  Remote boiler parameter transfer-enable &
                 // read/write flags
      CoolingControl,  // f8.8  Cooling control signal (%)
      TsetCH2,         // f8.8  Control setpoint for 2e CH circuit (°C)
      TrOverride,      // f8.8  Remote override room setpoint
      TSP,  // u8 / u8  Number of Transparent-Slave-Parameters supported by
            // slave
      TSPindexTSPvalue,  // u8 / u8  Index number / Value of referred-to
                         // transparent slave parameter.
      FHBsize,  // u8 / u8  Size of Fault-History-Buffer supported by slave
      FHBindexFHBvalue,       // u8 / u8  Index number / Value of referred-to
                              // fault-history buffer entry.
      MaxRelModLevelSetting,  // f8.8  Maximum relative modulation level setting
                              // (%)
      MaxCapacityMinModLevel,  // u8 / u8  Maximum boiler capacity (kW) /
                               // Minimum boiler modulation level(%)
      TrSet,                   // f8.8  Room Setpoint (°C)
      RelModLevel,             // f8.8  Relative Modulation Level (%)
      CHPressure,              // f8.8  Water pressure in CH circuit  (bar)
      DHWFlowRate,     // f8.8  Water flow rate in DHW circuit. (litres/minute)
      DayTime,         // special / u8  Day of Week and Time of Day
      Date,            // u8 / u8  Calendar date
      Year,            // u16  Calendar year
      TrSetCH2,        // f8.8  Room Setpoint for 2nd CH circuit (°C)
      Tr,              // f8.8  Room temperature (°C)
      Tboiler,         // f8.8  Boiler flow water temperature (°C)
      Tdhw,            // f8.8  DHW temperature (°C)
      Toutside,        // f8.8  Outside temperature (°C)
      Tret,            // f8.8  Return water temperature (°C)
      Tstorage,        // f8.8  Solar storage temperature (°C)
      Tcollector,      // f8.8  Solar collector temperature (°C)
      TflowCH2,        // f8.8  Flow water temperature CH2 circuit (°C)
      Tdhw2,           // f8.8  Domestic hot water temperature 2 (°C)
      Texhaust,        // s16  Boiler exhaust temperature (°C)
      Theatexchanger,  // f8.8 Heat exchanger temperature (°C)
      FanSpeed = 35,   // u16  Fan Speed (rpm)
      ElectricalCurrentBurnerFlame,  // f8.8 Electrical current through burner
                                     // flame (µA)
      TRoomCH2,          // f88  Room Temperature for 2nd CH circuit ("°C)
      RelativeHumidity,  // u8 / u8 Relative Humidity (%)
      TdhwSetUBTdhwSetLB =
          48,  // s8 / s8  DHW setpoint upper & lower bounds for adjustment (°C)
      MaxTSetUBMaxTSetLB,  // s8 / s8  Max CH water setpoint upper & lower
                           // bounds for adjustment  (°C)
      HcratioUBHcratioLB,  // s8 / s8  OTC heat curve ratio upper & lower bounds
                           // for adjustment
      Remoteparameter4boundaries,  // s8 / s8  Remote Parameter ratio upper &
                                   // lower bounds for adjustment
      Remoteparameter5boundaries,  // s8 / s8  Remote Parameter ratio upper &
                                   // lower bounds for adjustment
      Remoteparameter6boundaries,  // s8 / s8  Remote Parameter ratio upper &
                                   // lower bounds for adjustment
      Remoteparameter7boundaries,  // s8 / s8  Remote Parameter ratio upper &
                                   // lower bounds for adjustment
      Remoteparameter8boundaries,  // s8 / s8  Remote Parameter ratio upper &
                                   // lower bounds for adjustment
      TdhwSet = 56,  // f8.8  DHW setpoint (°C)    (Remote parameter 1)
      MaxTSet,       // f8.8  Max CH water setpoint (°C)  (Remote parameters 2)
      Hcratio,       // f8.8  OTC heat curve ratio (°C)  (Remote parameter 3)
      Remoteparameter4,  // f8.8  Remote parameter 4 (°C)  (Remote parameter 4)
      Remoteparameter5,  // f8.8  Remote parameter 5 (°C)  (Remote parameter 5)
      Remoteparameter6,  // f8.8  Remote parameter 6 (°C)  (Remote parameter 6)
      Remoteparameter7,  // f8.8  Remote parameter 7 (°C)  (Remote parameter 7)
      Remoteparameter8,  // f8.8  Remote parameter 8 (°C)  (Remote parameter 8)
      StatusVH = 70,     // flag8 / flag8 Status Ventilation/Heat recovery
      ControlSetpointVH,  // u8 Control setpoint V/H
      ASFFaultCodeVH,     // flag8 / u8 Aplication Specific Fault Flags/Code V/H
      DiagnosticCodeVH,   // u16 Diagnostic Code V/H
      ConfigMemberIDVH,   // flag8 / u8 Config/Member ID V/H
      OpenthermVersionVH,          // f8.8 OpenTherm Version V/H
      VersionTypeVH,               // u8 / u8 Version & Type V/H
      RelativeVentilation,         // u8 Relative Ventilation (%)
      RelativeHumidityExhaustAir,  // u8 / u8 Relative Humidity (%)
      CO2LevelExhaustAir,          // u16 CO2 Level (ppm)
      SupplyInletTemperature,      // f8.8 Supply Inlet Temperature (°C)
      SupplyOutletTemperature,     // f8.8 Supply Outlet Temperature(°C)
      ExhaustInletTemperature,     // f8.8 Exhaust Inlet Temperature (°C)
      ExhaustOutletTemperature,    // f8.8 Exhaust Outlet Temperature (°C)
      ActualExhaustFanSpeed,       // u16 Actual Exhaust Fan Speed (rpm)
      ActualSupplyFanSpeed,        // u16 Actual Supply Fan Speed (rpm)
      RemoteParameterSettingVH,    // flag8 / flag8 Remote Parameter Setting V/H
      NominalVentilationValue,     // u8 Nominal Ventilation Value
      TSPNumberVH,                 // u8 / u8 TSP Number V/H
      TSPEntryVH,                  // u8 / u8 TSP Entry V/H
      FaultBufferSizeVH,           // u8 / u8 Fault Buffer Size V/H
      FaultBufferEntryVH,          // u8 / u8 Fault Buffer Entry V/H
      RFstrengthbatterylevel = 98,  // u8 / u8  RF strength and battery level
      OperatingMode_HC1_HC2_DHW,    // u8 / u8 Operating Mode HC1, HC2/ DHW
      RemoteOverrideFunction,       // flag8 / -  Function of manual and program
                               // changes in master and remote room setpoint.
      SolarStorageMaster,    // flag8 / flag8  Solar Storage  Master flags.
      SolarStorageASFflags,  // flag8 / u8 / Solar Storage OEM-fault-code  flag8
                             // / u8  Application-specific fault flags and OEM
                             // fault code
      SolarStorageSlaveConfigMemberIDcode,  // flag8 / u8  Solar Storage Master
                                            // Configuration Flags /  Master
                                            // MemberID Code
      SolarStorageVersionType,       // u8 / u8 / Solar Storage product version
                                     // number and type
      SolarStorageTSP,               // u8 / u8 / Solar Storage Number of
                                     // Transparent-Slave-Parameters supported
      SolarStorageTSPindexTSPvalue,  // u8 / u8 / Solar Storage Index number /
                                     // Value of referred-to transparent slave
                                     // parameter
      SolarStorageFHBsize,           // u8 /u8 / Solar Storage Size of
                                     // Fault-History-Buffer supported by slave
      SolarStorageFHBindexFHBvalue,  // u8 /u8 / Solar Storage Index number /
                                     // Value of referred-to fault-history
                                     // buffer entry
      ElectricityProducerStarts,     // u16 Electricity producer starts
      ElectricityProducerHours,      // u16 Electricity producer hours
      ElectricityProduction,         // u16 Electricity production
      CumulativElectricityProduction,  // u16 Cumulativ Electricity production
      BurnerUnsuccessfulStarts,  // u16 Number of Un-successful burner starts
      FlameSignalTooLow,         // u16 Number of times flame signal too low
      OEMDiagnosticCode,         // u16  OEM-specific diagnostic/service code
      BurnerStarts,              // u16  Number of starts burner
      CHPumpStarts,              // u16  Number of starts CH pump
      DHWPumpValveStarts,        // u16  Number of starts DHW pump/valve
      DHWBurnerStarts,           // u16  Number of starts burner during DHW mode
      BurnerOperationHours,  // u16  Number of hours that burner is in operation
                             // (i.e. flame on)
      CHPumpOperationHours,  // u16  Number of hours that CH pump has been
                             // running
      DHWPumpValveOperationHours,  // u16  Number of hours that DHW pump has
                                   // been running or DHW valve has been opened
      DHWBurnerOperationHours,     // u16  Number of hours that burner is in
                                   // operation during DHW mode
      OpenThermVersionMaster,  // f8.8  The implemented version of the OpenTherm
                               // Protocol Specification in the master.
      OpenThermVersionSlave,   // f8.8  The implemented version of the OpenTherm
                               // Protocol Specification in the slave.
      MasterVersion,         // u8 / u8  Master product version number and type
      SlaveVersion,          // u8 / u8  Slave product version number and type
      RemehadFdUcodes,       // u8 / u8 Remeha dF-/dU-codes
      RemehaServicemessage,  // u8 / u8 Remeha Servicemessage
      RemehaDetectionConnectedSCU,  // u8 / u8 Remeha detection connected
                                    // SCU’s
    };

    union MasterStatus {
      struct {
        uint8_t CH : 1;
        uint8_t DHW : 1;
        uint8_t Cooling : 1;
        uint8_t OTC : 1;
        uint8_t CH2 : 1;
      };
      uint8_t value;
    };

    union SlaveStatus {
      struct {
        uint8_t Fault : 1;
        uint8_t CH : 1;
        uint8_t DHW : 1;
        uint8_t Flame : 1;
        uint8_t Cooling : 1;
        uint8_t CH2 : 1;
        uint8_t Diag : 1;
      };
      uint8_t value;
    };

    union Status {
      struct {
        SlaveStatus slave;
        MasterStatus master;
      };
      uint16_t value;
    };

    float fromF88(uint16_t v);
    uint16_t toF88(float v);

    union Message {
      struct {
        uint16_t value;
        DataId id : 8;
        uint8_t spare : 4;
        MessageType type : 3;
        bool parity : 1;
      };
      uint32_t frame;
      Message() : frame(0) {}
      Message(uint32_t fr) : frame(fr) {}
      Message(MessageType type, DataId id, uint16_t value) {
        init(type, id, value);
      }
      void init(MessageType type, DataId id, uint16_t value);
      void read(DataId id, uint16_t value = 0) {
        init(MessageType::ReadData, id, value);
      }
      void write(DataId id, uint16_t value) {
        init(MessageType::WriteData, id, value);
      }
      void write(DataId id, float value) {
        init(MessageType::WriteData, id, toF88(value));
      }
      void invalidate() {
        spare = 15;
      };
      void revalidate() {
        spare = 0;
      };
      bool isValid() const {
        return spare == 0;
      }
      Message(const Message &) = delete;
    };

    static_assert(sizeof(Message) == sizeof(uint32_t),
                  "Message must be 32 bit");

    union MasterConfig {
      struct {
        uint8_t memberId;
        uint8_t flags;
      };
      uint16_t value;
    };

    union SlaveConfig {
      struct {
        uint8_t memberId;
        union {
          struct {
            uint8_t dhw : 1;
            uint8_t ctype : 1;
            uint8_t cooling : 1;
            uint8_t dhwTank : 1;
            uint8_t pump : 1;
            uint8_t ch2 : 1;
          };
          uint8_t flags;
        };
      };
      uint16_t value;
    };

    union Config {
      struct {
        SlaveConfig slave;
        MasterConfig master;
      };
      uint32_t value;
    };

    union Fault {
      struct {
        uint8_t code;
        union {
          struct {
            uint8_t sr : 1;
            uint8_t rr : 1;
            uint8_t lwp : 1;
            uint8_t gf : 1;
            uint8_t ap : 1;
            uint8_t wot : 1;
          };
          uint8_t flags;
        };
      };
      uint16_t value;
    };

    union Rbp {
      struct {
        union {
          struct {
            uint8_t rw_dhw : 1;
            uint8_t rw_ch : 1;
          };
          uint8_t rw_flags;
        };
        union {
          struct {
            uint8_t te_dhw : 1;
            uint8_t te_ch : 1;
          };
          uint8_t te_flags;
        };
      };
      uint16_t value;
    };

    union Range {
      struct {
        uint8_t lower;
        uint8_t upper;
      };
      uint16_t value;
      bool isDefined() {
        return value != 0 && value != 0xFFFF && upper > lower;
      }
      void clamp(float &value) {
        if (!isDefined())
          return;
        if (value >= lower && value <= upper)
          return;
        if (isnan(value) || value < lower)
          value = lower;
        else
          value = upper;
      }
      bool fromJson(JsonArrayConst arr, bool *changed);
      void toJson(JsonArray arr);
    };

    struct Bounds {
      Range dhw;
      Range ch;
      Range hcr;
      size_t jsonSize();
      bool fromJson(JsonObjectConst hvac, bool *changed = nullptr);
      void toJson(JsonObject hvac);
    };

    struct Hvac {
      Status status = {};
      Config config = {};
      Fault fault = {};
      Rbp rbp = {};
      float tSet = NAN;
      float coolingControl = NAN;
      float tSetCh2 = NAN;
      float trOverride = NAN;
      std::vector<uint8_t> tsps;
      std::vector<uint8_t> fhb;
      float maxRelMod = NAN;
      union {
        struct {
          uint8_t maxCapacity;
          uint8_t minMod;
        };
        uint16_t maxCapaMinMod = 0;
      };
      float trSet = NAN;
      float relMod = NAN;
      float chPressure = NAN;
      float dhwFlow = NAN;
      float trSetCh2 = NAN;
      float tr = NAN;
      float tBoiler = NAN;
      float tDhw = NAN;
      float tOutside = NAN;
      float tRet = NAN;
      float tStorage = NAN;
      float tCollector = NAN;
      float tFlowCh2 = NAN;
      float tDhw2 = NAN;
      float tExhaust = NAN;
      float tHex = NAN;
      float bfCurrent = NAN;
      Bounds bounds = {};
      float tDhwSet = NAN;
      float maxTSet = NAN;
      float hcratio = NAN;
      uint16_t roFlags = 0;
      uint16_t diagCode = 0;
      float otverMaster = NAN;
      float otverSlave = NAN;
      uint16_t verMaster = 0;
      uint16_t verSlave = 0;
      Hvac(){};
      Hvac(const Hvac &) = delete;
      size_t jsonSize();
      void toJson(JsonObject obj);
    };

    enum class RecvState : int {
      Initial,
      InMessage,
      MessageValid,
      NoStartBit,
      Interference,
      NoStopBit,
      Parity,
      Incomplete,
      Timeout
    };

    struct Recv {
      Recv() {}
      Recv(const Recv &) = delete;
      void init();
      void consume(bool bit);
      void finalize();
      RecvState state;
      uint8_t mbuf;
      uint8_t mcnt;
      uint32_t buf;
      int pos;
    };

    union DataIdInfo {
      struct {
        uint8_t readableSet : 1;
        uint8_t readable : 1;
        uint8_t writableSet : 1;
        uint8_t writable : 1;
      };
      uint8_t value : 4;
      DataIdInfo(uint8_t v) : value(v) {}
    };

    const size_t IdInfoArrayLength = 256 /*total number of IDs*/ *
                                     4 /* bits per ID */ /
                                     (sizeof(uint32_t) * 8);
    struct IdInfoArray {
     public:
      DataIdInfo get(DataId id);
      void update(DataId id, DataIdInfo info);
      size_t jsonSize();
      bool fromJson(JsonObjectConst obj, bool *changed = nullptr);
      void toJson(JsonObject obj);

     private:
      uint32_t _arr[IdInfoArrayLength] = {};
    };

    class IDriver {
     public:
      virtual ~IDriver(){};
      virtual esp_err_t send(uint32_t frame) = 0;
      virtual esp_err_t receive(Recv &recv) = 0;
    };

    class Core : public virtual log::Loggable {
     public:
      Core() {}
      Core(const Core &) = delete;
      const char *name() const override {
        return _name;
      }

      Hvac &hvac() {
        return _hvac;
      }
      void setLogTraffic(bool value) {
        _logTraffic = value;
      }

     protected:
      std::mutex _mutex;
      Message _outgoing = {};
      TaskHandle_t _task = nullptr;
      Recv _recv;
      bool _logTraffic = false;
      Hvac _hvac;
      esp_err_t send(MessageType type, DataId id, uint16_t value);
      esp_err_t send(Message &msg) {
        return send(msg.type, msg.id, msg.value);
      }
      esp_err_t init(IDriver *driver, const char *name);
      esp_err_t transmit(bool retransmit = false);
      esp_err_t receive();
      void dumpFrame(int attempt);

     private:
      const char *_name;
      IDriver *_driver;
    };

    class Slave;
    class ISlaveModel : public INamed {
     public:
      ISlaveModel() {}
      ISlaveModel(const ISlaveModel &) = delete;
      virtual ~ISlaveModel() {}
      virtual void init(Slave *slave) {
        _slave = slave;
      }
      virtual bool processRequest(Message &msg) = 0;

     protected:
      Slave *_slave;
    };

    class Slave : public virtual Core {
     public:
      Slave() {}
      Slave(const Slave &) = delete;

     protected:
      esp_err_t init(opentherm::IDriver *driver, opentherm::ISlaveModel *model,
                     const char *name);
      virtual void processRequest(Message &msg);
      void run();

     private:
      ISlaveModel *_model;
    };

    const int MinCommDelay = 100;
    const int MaxCommDelay = 1000;
    const int DefaultCommDelay = MaxCommDelay;

    class Master;
    class IMasterModel : public INamed {
     public:
      IMasterModel() {}
      IMasterModel(const IMasterModel &) = delete;
      int commDelay() const {
        return _commDelay;
      }
      virtual ~IMasterModel() {}
      virtual void init(Master *master) {
        _master = master;
      }
      virtual bool nextRequest(Message &msg) = 0;
      virtual bool processResponse(Message &msg) = 0;

     protected:
      int _commDelay = DefaultCommDelay;
      Master *_master;
    };

    class Master : public virtual Core {
     public:
      Master() {}
      Master(const Master &) = delete;

      esp_err_t setJob(IMasterModel *job);
      IdInfoArray &ids() {
        return _ids;
      }

     protected:
      IdInfoArray _ids;
      esp_err_t init(opentherm::IDriver *driver, opentherm::IMasterModel *model,
                     const char *name);
      virtual bool isSupported(Message &msg);
      virtual void processResponse(Message &msg);
      virtual void incomingFrameInvalid() {}
      void run();
      virtual void jobDone(IMasterModel *job) {}
      int commDelay() {
        int delay = DefaultCommDelay;
        std::lock_guard lock(_mutex);
        if (_job)
          delay = _job->commDelay();
        else if (_model)
          delay = _model->commDelay();
        return delay;
      }

     private:
      IMasterModel *_model;
      IMasterModel *_job = nullptr;
      void commSlot();
    };

    class GenericMaster : public IMasterModel {
     public:
      const char *name() const override {
        return "otm";
      }

     protected:
      bool nextRequest(Message &msg) override;
      bool processResponse(Message &msg) override {
        return false;
      }

     private:
      int _index = 0;
    };

    class ImmergasMaster : public IMasterModel {
     public:
      ImmergasMaster() {
        _commDelay = MaxCommDelay / 2;
      }
      const char *name() const override {
        return "otm-img";
      }
      void init(Master *master) override {
        IMasterModel::init(master);
        auto &hvac = master->hvac();
        hvac.tSet = 35;
        hvac.maxTSet = 35;
        hvac.tDhw = 40;
        hvac.status.master.CH = 1;
        hvac.status.master.DHW = 1;
      }

     protected:
      bool nextRequest(Message &msg) override {
        auto &hvac = _master->hvac();
        if (_index < 0)
          _index = 0;
        if ((_mode % 10) == 0) {
          int ii = _index;
          _index++;
          switch (ii) {
            case 0: {
              Status s = hvac.status;
              s.slave.value = 0xCA;  // magic
              msg.read(DataId::Status, s.value);
            } break;
            case 1:
              msg.read(DataId::TdhwSetUBTdhwSetLB);
              break;
            case 2:
              msg.read(DataId::MaxTSetUBMaxTSetLB);
              break;
            case 3:
              msg.read(DataId::TSP);
              _tspi = 0;
              break;
            case 4:
              if (_tspi < hvac.tsps.size()) {
                msg.read(DataId::TSPindexTSPvalue, _tspi << 8);
                _tspi++;
                _index--;
              } else {
                msg.read(DataId::FHBsize);
                _fhbi = 0;
              }
              break;
            case 5:
              if (_fhbi < hvac.fhb.size()) {
                msg.read(DataId::FHBindexFHBvalue, _fhbi << 8);
                _fhbi++;
                _index--;
              } else {
                msg.read(DataId::MaxCapacityMinModLevel);
                _mode++;
                _index = 0;
              }
              break;
            default:
              _mode++;
              _index = 0;
              break;
          }
          return true;
        }
        int ii = _index % 3;
        int oi = _index / 3;
        _index++;
        switch (ii) {
          case 0: {
            Status s = hvac.status;
            s.slave.value = 0xCA;  // magic
            msg.read(DataId::Status, s.value);
          } break;
          case 1: {
            float tset = hvac.status.master.CH ? hvac.tSet : 0;
            msg.write(DataId::TSet, toF88(tset));
          } break;
          case 2:
            switch (oi) {
              case 0:
                msg.read(DataId::ASFflags);
                break;
              case 1:
                msg.read(DataId::RBPflags);
                break;
              case 2:
                msg.read(DataId::RelModLevel);
                break;
              case 3:
                msg.read(DataId::CHPressure);
                break;
              case 4:
                msg.read(DataId::Tboiler);
                break;
              case 5:
                msg.read(DataId::Tdhw);
                break;
              case 6:
                msg.read(DataId::Toutside);
                break;
              case 7:
                msg.read(DataId::ElectricalCurrentBurnerFlame);
                break;
              case 8:
                msg.write(DataId::TdhwSet, hvac.tDhwSet);
                break;
              case 9:
                msg.write(DataId::MaxTSet, toF88(hvac.maxTSet));
                _index = 0;
                _mode++;
                break;
              default:
                _index = 0;
                _mode++;
                break;
            }
            break;
          default:  // will never reach here, but keep compiler happy
            break;
        }
        return true;
      }
      bool processResponse(Message &msg) override {
        return false;
      }

     private:
      int _index = 0, _mode = 0, _tspi = 0, _fhbi = 0;
    };

    class ImmergasSlave : public ISlaveModel {
     public:
      const char *name() const override {
        return "ots-img";
      }
      void init(Slave *slave) override {
        ISlaveModel::init(slave);
        auto &hvac = slave->hvac();
        hvac.rbp.rw_dhw = 1;
        hvac.rbp.rw_ch = 1;
        hvac.rbp.te_dhw = 1;
        hvac.rbp.te_dhw = 1;
        hvac.bounds.dhw.lower = 35;
        hvac.bounds.dhw.upper = 55;
        hvac.bounds.ch.lower = 35;
        hvac.bounds.ch.upper = 80;
        hvac.chPressure = 1.5;
      }
      bool processRequest(Message &msg) override {
        return false;
      }
    };
  }  // namespace opentherm

  namespace dev {

    class OpenthermMaster : public virtual opentherm::Master,
                            public virtual Device {
     public:
      OpenthermMaster(opentherm::IDriver *driver,
                      opentherm::IMasterModel *model, const char *name);
      OpenthermMaster(const OpenthermMaster &) = delete;
      bool handleRequest(Request &req) override;

     protected:
      void processResponse(opentherm::Message &msg) override;
      void incomingFrameInvalid() override;
      void jobDone(opentherm::IMasterModel *job) override;

      void setState(const JsonVariantConst cfg,
                    DynamicJsonDocument **result) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(RequestContext &ctx) override;
      bool pollSensors() override;

     private:
      Response *_pendingResponse = nullptr;
      std::mutex _mutexPR;
    };

    class OpenthermSlave : public virtual opentherm::Slave,
                           public virtual Device {
     public:
      OpenthermSlave(opentherm::IDriver *driver, opentherm::ISlaveModel *model,
                     const char *name);
      OpenthermSlave(const OpenthermSlave &) = delete;

     protected:
      void setState(const JsonVariantConst cfg,
                    DynamicJsonDocument **result) override;
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      bool setConfig(const JsonVariantConst cfg,
                     DynamicJsonDocument **result) override;
      DynamicJsonDocument *getConfig(RequestContext &ctx) override;
    };

    OpenthermMaster *useOpenthermMaster(
        gpio_num_t rx, gpio_num_t tx, opentherm::IMasterModel *model = nullptr,
        const char *name = nullptr);
    OpenthermSlave *useOpenthermSlave(gpio_num_t rx, gpio_num_t tx,
                                      opentherm::ISlaveModel *model = nullptr,
                                      const char *name = nullptr);
  }  // namespace dev
}  // namespace esp32m