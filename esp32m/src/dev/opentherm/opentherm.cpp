#include "esp32m/dev/opentherm.hpp"
#include "esp32m/config/changed.hpp"

#include <esp_task_wdt.h>

namespace esp32m {
  namespace opentherm {

    static const char *MessageTypeNames[] = {"r",  "w",  "-",  "?",
                                             "r+", "w+", "d-", "--"};

    static const char *RecvStateNames[] = {
        "initial",      "in-message",   "valid",
        "no-start-bit", "interference", "no-stop-bit",
        "parity-err",   "incomplete",   "timeout"};

#ifdef __builtin_parity
#  define isOdd __builtin_parity
#else
    bool isOdd(uint32_t frame) {
      uint32_t y = frame ^ (frame >> 1);
      y = y ^ (y >> 2);
      y = y ^ (y >> 4);
      y = y ^ (y >> 8);
      y = y ^ (y >> 16);
      return y & 1;
    }
#endif

    float fromF88(uint16_t v) {
      return (int16_t)v / 256.0f;
    }

    uint16_t toF88(float v) {
      return (uint16_t)(v * 256);
    }

    uint16_t toF88(float v, MessageType &rt) {
      if (isnan(v)) {
        rt = MessageType::DataInvalid;
        return 0;
      }
      return (uint16_t)(v * 256);
    }

    bool Range::fromJson(JsonArrayConst arr, bool *changed) {
      if (arr && arr.size() == 2) {
        json::set(lower, arr[0], changed);
        json::set(upper, arr[1], changed);
        return true;
      }
      return false;
    }

    void Range::toJson(JsonArray arr) {
      arr.add(lower);
      arr.add(upper);
    }
    size_t Bounds::jsonSize() {
      int n = 0;
      if (dhw.isDefined())
        n++;
      if (ch.isDefined())
        n++;
      if (hcr.isDefined())
        n++;
      if (n > 0)
        n = JSON_OBJECT_SIZE(1) /* bounds */ +
            n * (JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(2));
      return n;
    }
    bool Bounds::fromJson(JsonObjectConst hvac, bool *changed) {
      auto bounds = hvac["bounds"].as<JsonObjectConst>();
      if (!bounds)
        return false;
      return dhw.fromJson(bounds["dhw"], changed) ||
             ch.fromJson(bounds["ch"], changed) ||
             hcr.fromJson(bounds["hcr"], changed);
    }
    void Bounds::toJson(JsonObject hvac) {
      bool hasdhw = dhw.isDefined();
      bool hasch = ch.isDefined();
      bool hashcr = hcr.isDefined();
      if (hasdhw || hasch || hashcr) {
        auto bounds = hvac.createNestedObject("bounds");
        if (hasdhw)
          dhw.toJson(bounds.createNestedArray("dhw"));
        if (hasch)
          ch.toJson(bounds.createNestedArray("ch"));
        if (hashcr)
          hcr.toJson(bounds.createNestedArray("hcr"));
      }
    }

    void toJson(JsonObject o, const char *key, std::vector<uint8_t> &v) {
      if (v.size()) {
        auto arr = o.createNestedArray(key);
        for (auto i : v) arr.add(i);
      }
    }

    size_t jsonSize(std::vector<uint8_t> &v) {
      auto s = v.size();
      if (s)
        s = JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(s);
      return s;
    }

    size_t Hvac::jsonSize() {
      return JSON_OBJECT_SIZE(1 /*hvac*/ + 37) + bounds.jsonSize() +
             opentherm::jsonSize(tsps) + opentherm::jsonSize(fhb);
    }

    void Hvac::toJson(JsonObject obj) {
      auto hvac = obj.createNestedObject("hvac");
      hvac["status"] = status.value;
      json::to(hvac, "ts", tSet);
      if (config.value)
        hvac["cfg"] = config.value;
      if (fault.value)
        hvac["fault"] = fault.value;
      if (rbp.value)
        hvac["rbp"] = rbp.value;
      json::to(hvac, "cc", coolingControl);
      json::to(hvac, "tsch2", tSetCh2);
      json::to(hvac, "trov", trOverride);
      opentherm::toJson(hvac, "tsps", tsps);
      opentherm::toJson(hvac, "fhb", fhb);
      json::to(hvac, "maxrm", maxRelMod);
      if (maxCapacity)
        hvac["maxcap"] = maxCapacity;
      if (minMod)
        hvac["minmod"] = minMod;
      json::to(hvac, "trs", trSet);
      json::to(hvac, "rm", relMod);
      json::to(hvac, "chp", chPressure);
      json::to(hvac, "dhwf", dhwFlow);
      json::to(hvac, "trsch2", trSetCh2);
      json::to(hvac, "tr", tr);
      json::to(hvac, "tb", tBoiler);
      json::to(hvac, "tdhw", tDhw);
      json::to(hvac, "to", tOutside);
      json::to(hvac, "tret", tRet);
      json::to(hvac, "tst", tStorage);
      json::to(hvac, "tc", tCollector);
      json::to(hvac, "tfch2", tFlowCh2);
      json::to(hvac, "tdhw2", tDhw2);
      json::to(hvac, "tex", tExhaust);
      json::to(hvac, "thex", tHex);
      json::to(hvac, "bfc", bfCurrent);
      bounds.toJson(hvac);
      json::to(hvac, "tdhws", tDhwSet);
      json::to(hvac, "maxts", maxTSet);
      json::to(hvac, "hcr", hcratio);
      if (roFlags)
        hvac["rof"] = roFlags;
      if (roFlags)
        hvac["diag"] = diagCode;
      json::to(hvac, "otvm", otverMaster);
      json::to(hvac, "otvs", otverSlave);
      if (verMaster)
        hvac["vm"] = verMaster;
      if (verSlave)
        hvac["vs"] = verSlave;
    }

    DataIdInfo IdInfoArray::get(DataId id) {
      auto i = static_cast<uint8_t>(id);
      return static_cast<DataIdInfo>((_arr[i >> 3] >> ((i & 7) << 2)) & 15);
    }

    void IdInfoArray::update(DataId id, DataIdInfo info) {
      auto i = static_cast<uint8_t>(id);
      uint32_t slot = _arr[i >> 3];
      int shift = (i & 7) << 2;
      uint32_t old = (slot >> shift) & 15;
      uint32_t neu = info.value & 15;
      if (old == neu)
        return;
      uint32_t mask = 15 << shift;
      _arr[i >> 3] = (slot & ~mask) | (neu << shift);
    }
    size_t IdInfoArray::jsonSize() {
      for (int i = 0; i < IdInfoArrayLength; i++)
        if (_arr[i] != 0)
          return JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(IdInfoArrayLength);
      return 0;
    }
    bool IdInfoArray::fromJson(JsonObjectConst obj, bool *changed) {
      auto ids = obj["ids"].as<JsonArrayConst>();
      if (!ids)
        return false;
      for (int i = 0; i < ids.size() && i < IdInfoArrayLength; i++) {
        uint32_t ov = _arr[i];
        uint32_t v = ids[i].as<uint32_t>();
        if (changed && v != ov)
          *changed = true;
        _arr[i] = v;
      }
      return true;
    }
    void IdInfoArray::toJson(JsonObject obj) {
      if (jsonSize()) {
        auto ids = obj.createNestedArray("ids");
        for (int i = 0; i < IdInfoArrayLength; i++) ids.add(_arr[i]);
      }
    }

    void Message::init(MessageType type, DataId id, uint16_t value) {
      this->frame = 0;
      this->type = type;
      this->id = id;
      this->value = value;
      if (isOdd(this->frame))
        this->parity = 1;
    }

    void Recv::init() {
      state = RecvState::Initial;
      mbuf = 0;
      mcnt = 0;
      buf = 0;
      pos = 0;
    }

    void Recv::consume(bool bit) {
      const int ManchesterOne = 2;
      const int ManchesterZero = 1;
      mbuf = (mbuf << 1) | bit;
      mcnt++;
      if (mcnt < 2)
        return;
      auto encodedBit = mbuf & 0x3;

      switch (state) {
        case RecvState::Initial:
          if (encodedBit == ManchesterOne) {
            state = RecvState::InMessage;
            mcnt = 0;
          } else {
            mcnt--;
          }
          break;
        case RecvState::InMessage:
          if (pos < 32)
            switch (encodedBit) {
              case ManchesterOne:
                buf |= (1 << (31 - pos));
                /* fallthrough */
              case ManchesterZero:
                pos++;
                mcnt = 0;
                break;
              default:
                state = RecvState::Interference;
                break;
            }
          else if (encodedBit != ManchesterOne) {
            state = RecvState::NoStopBit;
          } else if (isOdd(buf)) {
            state = RecvState::Parity;
          } else
            state = RecvState::MessageValid;
          break;
        default:
          break;
      }
    }

    void Recv::finalize() {
      switch (state) {
        case RecvState::Initial:
        case RecvState::InMessage:
          state = RecvState::Incomplete;
          break;
        default:
          break;
      }
    }

    esp_err_t Core::init(IDriver *driver, const char *name) {
      _name = name;
      _driver = driver;
      _outgoing.invalidate();
      return ESP_OK;
    }

    esp_err_t Core::send(MessageType type, DataId id, uint16_t value) {
      std::lock_guard lock(_mutex);
      if (_outgoing.isValid())
        return ESP_ERR_INVALID_STATE;
      _outgoing.init(type, id, value);
      return ESP_OK;
    }

    esp_err_t Core::transmit(bool retransmit) {
      uint32_t frame;
      {
        std::lock_guard lock(_mutex);
        if (!_outgoing.isValid()) {
          if (retransmit)
            _outgoing.revalidate();
          else
            return ESP_ERR_INVALID_STATE;
        }
        frame = _outgoing.frame;
      }
      ESP_CHECK_RETURN(_driver->send(frame));
      {
        std::lock_guard lock(_mutex);
        _outgoing.invalidate();
      }
      // logD("transmitted: %i %i %i", _outgoing.type, _outgoing.id,
      // _outgoing.value);
      return ESP_OK;
    }

    esp_err_t Core::receive() {
      _recv.init();
      ESP_CHECK_RETURN(_driver->receive(_recv));
      return ESP_OK;
    }

    void Core::dumpFrame(int attempt) {
      logD("recv state: %s, value=0x%" PRIx32 ", bits=%i, attempt=%i",
           RecvStateNames[static_cast<int>(_recv.state)], _recv.buf, _recv.pos,
           attempt);
    }

    /* --- SLAVE --- */

    esp_err_t Slave::init(opentherm::IDriver *driver,
                          opentherm::ISlaveModel *model, const char *name) {
      ESP_CHECK_RETURN(Core::init(
          driver, name ? name : (model ? model->name() : "ot-slave")));
      _model = model;
      if (_model)
        _model->init(this);
      _hvac.bounds.dhw.clamp(_hvac.tDhwSet);
      _hvac.bounds.ch.clamp(_hvac.maxTSet);
      _hvac.bounds.hcr.clamp(_hvac.hcratio);
      xTaskCreate([](void *self) { ((Slave *)self)->run(); },
                  makeTaskName(this->name()), 4096, this, 2, &_task);
      return ESP_OK;
    }

    void Slave::processRequest(Message &msg) {
      if (_model)
        if (_model->processRequest(msg))
          return;
      MessageType rt = MessageType::UnknownDataId;
      uint16_t rv = 0;
      switch (msg.type) {
        case MessageType::ReadData:
          rt = MessageType::ReadAck;
          switch (msg.id) {
            case DataId::Status:
              _hvac.status.master.value = (msg.value >> 8) & 0xff;
              rv = _hvac.status.value;
              break;
            case DataId::TSet:
              rv = toF88(_hvac.tSet, rt);
              break;
            case DataId::MConfigMMemberIDcode:
              rv = _hvac.config.master.value;
              break;
            case DataId::SConfigSMemberIDcode:
              rv = _hvac.config.slave.value;
              break;
            case DataId::ASFflags:
              rv = _hvac.fault.value;
              break;
            case DataId::RBPflags:
              rv = _hvac.fault.value;
              break;
            case DataId::CoolingControl:
              rv = toF88(_hvac.coolingControl, rt);
              break;
            case DataId::TsetCH2:
              rv = toF88(_hvac.tSetCh2, rt);
              break;
            case DataId::TrOverride:
              rv = toF88(_hvac.trOverride, rt);
              break;
            case DataId::TSP:
              rv = _hvac.tsps.size() << 8;
              break;
            case DataId::TSPindexTSPvalue: {
              int i = msg.value >> 8;
              if (i >= _hvac.tsps.size())
                rt = MessageType::DataInvalid;
              else
                rv = (i << 8) | _hvac.tsps[i];
            } break;
            case DataId::FHBsize:
              rv = _hvac.fhb.size() << 8;
              break;
            case DataId::FHBindexFHBvalue: {
              int i = msg.value >> 8;
              if (i >= _hvac.fhb.size())
                rt = MessageType::DataInvalid;
              else
                rv = (i << 8) | _hvac.fhb[i];
            } break;
            case DataId::MaxRelModLevelSetting:
              rv = toF88(_hvac.maxRelMod, rt);
              break;
            case DataId::MaxCapacityMinModLevel:
              rv = _hvac.maxCapaMinMod;
              break;
            case DataId::TrSet:
              rv = toF88(_hvac.trSet, rt);
              break;
            case DataId::RelModLevel:
              rv = toF88(_hvac.relMod, rt);
              break;
            case DataId::CHPressure:
              rv = toF88(_hvac.chPressure, rt);
              break;
            case DataId::DHWFlowRate:
              rv = toF88(_hvac.dhwFlow, rt);
              break;
            case DataId::TrSetCH2:
              rv = toF88(_hvac.trSetCh2, rt);
              break;
            case DataId::Tr:
              rv = toF88(_hvac.tr, rt);
              break;
            case DataId::Tboiler:
              rv = toF88(_hvac.tBoiler, rt);
              break;
            case DataId::Tdhw:
              rv = toF88(_hvac.tDhw, rt);
              break;
            case DataId::Toutside:
              rv = toF88(_hvac.tOutside, rt);
              break;
            case DataId::Tret:
              rv = toF88(_hvac.tRet, rt);
              break;
            case DataId::Tstorage:
              rv = toF88(_hvac.tStorage, rt);
              break;
            case DataId::Tcollector:
              rv = toF88(_hvac.tCollector, rt);
              break;
            case DataId::TflowCH2:
              rv = toF88(_hvac.tFlowCh2, rt);
              break;
            case DataId::Tdhw2:
              rv = toF88(_hvac.tDhw2, rt);
              break;
            case DataId::Texhaust:
              rv = toF88(_hvac.tExhaust, rt);
              break;
            case DataId::Theatexchanger:
              rv = toF88(_hvac.tHex, rt);
              break;
            case DataId::ElectricalCurrentBurnerFlame:
              rv = toF88(_hvac.bfCurrent, rt);
              break;
            case DataId::TdhwSetUBTdhwSetLB:
              rv = _hvac.bounds.dhw.value;
              break;
            case DataId::MaxTSetUBMaxTSetLB:
              rv = _hvac.bounds.ch.value;
              break;
            case DataId::HcratioUBHcratioLB:
              rv = _hvac.bounds.hcr.value;
              break;
            case DataId::TdhwSet:
              rv = toF88(_hvac.tDhwSet, rt);
              break;
            case DataId::MaxTSet:
              rv = toF88(_hvac.maxTSet, rt);
              break;
            case DataId::Hcratio:
              rv = toF88(_hvac.hcratio, rt);
              break;
            case DataId::RemoteOverrideFunction:
              rv = _hvac.roFlags;
              break;
            case DataId::OEMDiagnosticCode:
              rv = _hvac.diagCode;
              break;
            case DataId::OpenThermVersionMaster:
              rv = toF88(_hvac.otverMaster, rt);
              break;
            case DataId::OpenThermVersionSlave:
              rv = toF88(_hvac.otverSlave, rt);
              break;
            case DataId::MasterVersion:
              rv = _hvac.verMaster;
              break;
            case DataId::SlaveVersion:
              rv = _hvac.verSlave;
              break;
            default:
              rt = MessageType::UnknownDataId;
              break;
          }
          break;
        case MessageType::WriteData:
          rt = MessageType::WriteAck;
          switch (msg.id) {
            case DataId::Status:
              _hvac.status.master.value = (msg.value >> 8) & 0xff;
              rv = _hvac.status.value;
              break;
            case DataId::TSet:
              _hvac.tSet = fromF88(msg.value);
              if (_hvac.tSet != 0)
                _hvac.bounds.ch.clamp(_hvac.tSet);
              rv = toF88(_hvac.tSet);
              break;
            case DataId::MConfigMMemberIDcode:
              _hvac.config.master.value = rv = msg.value;
              break;
            case DataId::CoolingControl:
              _hvac.coolingControl = fromF88(rv = msg.value);
              break;
            case DataId::TsetCH2:
              _hvac.tSetCh2 = fromF88(rv = msg.value);
              if (_hvac.tSetCh2 != 0)
                _hvac.bounds.ch.clamp(_hvac.tSetCh2);
              rv = toF88(_hvac.tSetCh2);
              break;
            case DataId::TSPindexTSPvalue: {
              int i = msg.value >> 8;
              if (i >= _hvac.tsps.size())
                rt = MessageType::DataInvalid;
              else {
                _hvac.tsps[i] = msg.value & 0xff;
                rv = msg.value;
              }
            } break;
            case DataId::MaxRelModLevelSetting:
              _hvac.maxRelMod = fromF88(rv = msg.value);
              break;
            case DataId::TrSet:
              _hvac.trSet = fromF88(rv = msg.value);
              break;
            case DataId::TrSetCH2:
              _hvac.trSetCh2 = fromF88(rv = msg.value);
              break;
            case DataId::Tr:
              _hvac.tr = fromF88(rv = msg.value);
              break;
            case DataId::TdhwSet:
              _hvac.tDhwSet = fromF88(msg.value);
              _hvac.bounds.dhw.clamp(_hvac.tDhwSet);
              rv = toF88(_hvac.tDhwSet);
              break;
            case DataId::MaxTSet:
              _hvac.maxTSet = fromF88(msg.value);
              _hvac.bounds.ch.clamp(_hvac.maxTSet);
              rv = toF88(_hvac.maxTSet);
              break;
            case DataId::Hcratio:
              _hvac.hcratio = fromF88(msg.value);
              _hvac.bounds.hcr.clamp(_hvac.hcratio);
              rv = toF88(_hvac.hcratio);
              break;
            case DataId::OpenThermVersionMaster:
              _hvac.otverMaster = fromF88(rv = msg.value);
              break;
            case DataId::MasterVersion:
              _hvac.verMaster = msg.value;
              break;

            default:
              rt = MessageType::UnknownDataId;
              break;
          }
          break;
        case MessageType::InvalidData:
          rt = MessageType::DataInvalid;
          switch (msg.id) {
            case DataId::TSet:
              _hvac.tSet = NAN;
              break;
            case DataId::CoolingControl:
              _hvac.coolingControl = NAN;
              break;
            case DataId::TsetCH2:
              _hvac.tSetCh2 = NAN;
              break;
            case DataId::MaxRelModLevelSetting:
              _hvac.maxRelMod = NAN;
              break;
            case DataId::TrSet:
              _hvac.trSet = NAN;
              break;
            case DataId::TrSetCH2:
              _hvac.trSetCh2 = NAN;
              break;
            case DataId::Tr:
              _hvac.tr = NAN;
              break;
            case DataId::TdhwSet:
              _hvac.tDhwSet = NAN;
              break;
            case DataId::MaxTSet:
              _hvac.maxTSet = NAN;
              break;
            case DataId::Hcratio:
              _hvac.hcratio = NAN;
              break;
            case DataId::OpenThermVersionMaster:
              _hvac.otverMaster = NAN;
              break;
            default:
              break;
          }
        default:
          break;
      }
      if (_logTraffic)
        logD("ID %i: %s %i -> %s %i", static_cast<int>(msg.id),
             MessageTypeNames[static_cast<int>(msg.type)], msg.value,
             MessageTypeNames[static_cast<int>(rt)], rv);
      send(rt, msg.id, rv);
    }

    void Slave::run() {
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        esp_err_t err = ESP_ERROR_CHECK_WITHOUT_ABORT(receive());
        if (err == ESP_OK) {
          if (_recv.state == RecvState::MessageValid) {
            Message msg(_recv.buf);
            processRequest(msg);
          } else
            dumpFrame(0);
        }
        delay(
            100);  // delay after the request according to OT spec - 20..800 ms
        transmit();
        delay(50);  // min. delay before next request from master is 100ms, but
                    // we wait half of that just in case
      }
    }

    /* --- MASTER --- */
    esp_err_t Master::init(opentherm::IDriver *driver,
                           opentherm::IMasterModel *model, const char *name) {
      ESP_CHECK_RETURN(Core::init(
          driver, name ? name : (model ? model->name() : "ot-master")));
      _model = model;
      if (!_model)
        _model = new GenericMaster();
      _model->init(this);
      xTaskCreate([](void *self) { ((Master *)self)->run(); },
                  makeTaskName(this->name()), 4096, this, 10, &_task);
      return ESP_OK;
    }

    bool Master::isSupported(Message &msg) {
      auto idInfo = _ids.get(msg.id);
      switch (msg.type) {
        case MessageType::ReadData:
          return idInfo.readable || !idInfo.readableSet;
        case MessageType::WriteData:
        case MessageType::InvalidData:
          return idInfo.writable || !idInfo.writableSet;
        default:
          return false;
      }
    }

    esp_err_t Master::setJob(IMasterModel *job) {
      if (!job)
        return ESP_ERR_INVALID_ARG;
      job->init(this);
      {
        std::lock_guard lock(_mutex);
        if (!_job) {
          _job = job;
          return ESP_OK;
        }
      }
      delete job;
      return ESP_ERR_INVALID_ARG;
    }

    void Master::processResponse(Message &msg) {
      if (_logTraffic)
        logD("ID %i: %s %i -> %s %i", static_cast<int>(_outgoing.id),
             MessageTypeNames[static_cast<int>(_outgoing.type)],
             _outgoing.value, MessageTypeNames[static_cast<int>(msg.type)],
             msg.value);
      auto idInfo = _ids.get(msg.id);
      switch (msg.type) {
        case MessageType::ReadAck:
          idInfo.readableSet = true;
          idInfo.readable = true;
          break;
        case MessageType::WriteAck:
          idInfo.writableSet = true;
          idInfo.writable = true;
          break;
        case MessageType::DataInvalid:
          if (_outgoing.type == MessageType::ReadData) {
            idInfo.readableSet = true;
            idInfo.readable = true;
          } else if (_outgoing.type == MessageType::WriteData ||
                     _outgoing.type == MessageType::InvalidData) {
            idInfo.writableSet = true;
            idInfo.writable = true;
          }
          break;
        case MessageType::UnknownDataId:
          if (_outgoing.type == MessageType::ReadData) {
            idInfo.readableSet = true;
            idInfo.readable = false;
          } else if (_outgoing.type == MessageType::WriteData ||
                     _outgoing.type == MessageType::InvalidData) {
            idInfo.writableSet = true;
            idInfo.writable = false;
          }
          break;
        default:
          break;
      }
      _ids.update(msg.id, idInfo);
      IMasterModel *job;
      {
        std::lock_guard lock(_mutex);
        job = _job;
      }
      if (job)
        if (job->processResponse(msg))
          return;
      if (_model)
        if (_model->processResponse(msg))
          return;
      if (msg.type == MessageType::ReadAck || msg.type == MessageType::WriteAck)
        switch (msg.id) {
          case DataId::Status:
            _hvac.status.slave.value = msg.value & 0xff;
            break;
          case DataId::TSet:
            break;
          case DataId::SConfigSMemberIDcode:
            _hvac.config.slave.value = msg.value;
            break;
          case DataId::ASFflags:
            _hvac.fault.value = msg.value;
            break;
          case DataId::RBPflags:
            _hvac.rbp.value = msg.value;
            break;
          case DataId::CoolingControl:
            _hvac.coolingControl = fromF88(msg.value);
            break;
          case DataId::TsetCH2:
            _hvac.tSetCh2 = fromF88(msg.value);
            break;
          case DataId::TrOverride:
            _hvac.trOverride = fromF88(msg.value);
            break;
          case DataId::TSP:
            _hvac.tsps.resize(msg.value >> 8);
            break;
          case DataId::TSPindexTSPvalue: {
            int i = msg.value >> 8;
            if (i > _hvac.tsps.size())
              _hvac.tsps.resize(i + 1);
            _hvac.tsps[i] = msg.value & 0xff;
          } break;
          case DataId::FHBsize:
            _hvac.fhb.resize(msg.value >> 8);
            break;
          case DataId::FHBindexFHBvalue: {
            int i = msg.value >> 8;
            if (i > _hvac.fhb.size())
              _hvac.fhb.resize(i + 1);
            _hvac.fhb[i] = msg.value & 0xff;
          } break;
          case DataId::MaxRelModLevelSetting:
            _hvac.maxRelMod = fromF88(msg.value);
            break;
          case DataId::MaxCapacityMinModLevel:
            _hvac.maxCapaMinMod = msg.value;
            break;
          case DataId::TrSet:
            _hvac.trSet = fromF88(msg.value);
            break;
          case DataId::RelModLevel:
            _hvac.relMod = fromF88(msg.value);
            break;
          case DataId::CHPressure:
            _hvac.chPressure = fromF88(msg.value);
            break;
          case DataId::DHWFlowRate:
            _hvac.dhwFlow = fromF88(msg.value);
            break;
          case DataId::TrSetCH2:
            _hvac.trSetCh2 = fromF88(msg.value);
            break;
          case DataId::Tr:
            _hvac.tr = fromF88(msg.value);
            break;
          case DataId::Tboiler:
            _hvac.tBoiler = fromF88(msg.value);
            break;
          case DataId::Tdhw:
            _hvac.tDhw = fromF88(msg.value);
            break;
          case DataId::Toutside:
            _hvac.tOutside = fromF88(msg.value);
            break;
          case DataId::Tret:
            _hvac.tRet = fromF88(msg.value);
            break;
          case DataId::Tstorage:
            _hvac.tStorage = fromF88(msg.value);
            break;
          case DataId::Tcollector:
            _hvac.tCollector = fromF88(msg.value);
            break;
          case DataId::TflowCH2:
            _hvac.tFlowCh2 = fromF88(msg.value);
            break;
          case DataId::Tdhw2:
            _hvac.tDhw2 = fromF88(msg.value);
            break;
          case DataId::Texhaust:
            _hvac.tExhaust = fromF88(msg.value);
            break;
          case DataId::Theatexchanger:
            _hvac.tHex = fromF88(msg.value);
            break;
          case DataId::ElectricalCurrentBurnerFlame:
            _hvac.bfCurrent = fromF88(msg.value);
            break;
          case DataId::TdhwSetUBTdhwSetLB:
            _hvac.bounds.dhw.value = msg.value;
            break;
          case DataId::MaxTSetUBMaxTSetLB:
            _hvac.bounds.ch.value = msg.value;
            break;
          case DataId::HcratioUBHcratioLB:
            _hvac.bounds.hcr.value = msg.value;
            break;
          case DataId::TdhwSet:
            _hvac.tDhwSet = fromF88(msg.value);
            break;
          case DataId::MaxTSet:
            _hvac.maxTSet = fromF88(msg.value);
            break;
          case DataId::Hcratio:
            _hvac.hcratio = fromF88(msg.value);
            break;
          case DataId::RemoteOverrideFunction:
            _hvac.roFlags = msg.value;
            break;
          case DataId::OEMDiagnosticCode:
            _hvac.diagCode = msg.value;
            break;
          case DataId::OpenThermVersionMaster:
            _hvac.otverMaster = fromF88(msg.value);
            break;
          case DataId::OpenThermVersionSlave:
            _hvac.otverSlave = fromF88(msg.value);
            break;
          case DataId::MasterVersion:
            _hvac.verMaster = msg.value;
            break;
          case DataId::SlaveVersion:
            _hvac.verSlave = msg.value;
            break;

          default:
            break;
        }
    }

    void Master::commSlot() {
      IMasterModel *job;
      {
        std::lock_guard lock(_mutex);
        job = _job;
      }
      Message msg;
      for (int counter = 0; counter < 256; counter++) {
        if (job) {
          if (job->nextRequest(msg)) {
            send(msg);
            break;
          }
          {
            std::lock_guard lock(_mutex);
            _job = nullptr;
          }
          jobDone(job);
          delete job;
          job = nullptr;
        }
        if (_model && _model->nextRequest(msg) && isSupported(msg)) {
          send(msg);
          break;
        }
      }
    }

    void Master::run() {
      const int MaxAttempts = 3;
      esp_task_wdt_add(NULL);
      for (;;) {
        esp_task_wdt_reset();
        commSlot();

        int attempt = 0;
        if (transmit() == ESP_OK)
          while (true) {
            // should we have a delay here at all? Spec says slave
            // must wait at least 20ms before responding, but in fact
            // some respond instantly. On the other hand, there are delayed
            // fluctuations on RX pin during transmission via simple OT<->TTL
            // adapters, that may be picked up by the receiver. So we must wait
            // some time after transmission to make sure RX line calms down. 1ms
            // seems reasonable.
            // delay(1);
            esp_err_t err = ESP_ERROR_CHECK_WITHOUT_ABORT(receive());
            if (err == ESP_OK) {
              if (_recv.state == RecvState::MessageValid) {
                Message msg(_recv.buf);
                processResponse(msg);
                break;
              } else
                dumpFrame(attempt);
            }
            if (attempt++ >= MaxAttempts) {
              if (err == ESP_OK)
                incomingFrameInvalid();
              break;
            }
            transmit(true);
            esp_task_wdt_reset();
            delay(100 * attempt);  // min 100 ms delay between messages
          }
        delay(commDelay());
      }
    }

    class ScanJob : public IMasterModel {
     public:
      ScanJob() {
        _commDelay = MinCommDelay;
      }
      const char *name() const override {
        return "scan";
      }
      bool nextRequest(Message &msg) override {
        for (;;) {
          if (_index >= 256)
            return false;
          auto idInfo = _master->ids().get(static_cast<DataId>(_index));
          if (idInfo.readableSet)
            _index++;
          else
            break;
        }
        msg.read(static_cast<DataId>(_index));
        _index++;
        return true;
      }
      bool processResponse(Message &msg) override {
        return false;
      }

     private:
      int _index = 0;
    };

    class SnapshotJob : public IMasterModel {
     public:
      SnapshotJob() {
        _commDelay = MinCommDelay;
      }
      const char *name() const override {
        return "snapshot";
      }
      bool nextRequest(Message &msg) override {
        for (;;) {
          if (_index >= 256)
            return false;
          auto idInfo = _master->ids().get(static_cast<DataId>(_index));
          if (idInfo.readableSet)
            break;
          else
            _index++;
        }
        msg.read(static_cast<DataId>(_index));
        _index++;
        return true;
      }
      bool processResponse(Message &msg) override {
        if (msg.type == MessageType::ReadAck)
          values[_index] = msg.value;
        return true;
      }
      std::map<uint8_t, uint16_t> values;

     private:
      int _index = 0;
    };

    bool GenericMaster::nextRequest(Message &msg) {
      auto &hvac = _master->hvac();
      if (_index < 0)
        _index = 0;
      switch (_index++) {
        case 0:
          msg.read(DataId::Status, hvac.status.value);
          break;
        case 1:
          msg.write(DataId::TSet, toF88(hvac.tSet));
          break;
        case 2:
          msg.read(DataId::Tboiler);
          break;
        default:
          _index = 0;
          break;
      }
      return true;
    }

    typedef opentherm::IDriver *(*DriverConstructor)(gpio_num_t rxPin,
                                                     gpio_num_t txPin);
#ifdef ESP32M_USE_DEPRECATED_RMT
    extern opentherm::IDriver *newDeprecatedRmtDriver(gpio_num_t rxPin,
                                                      gpio_num_t txPin);
    DriverConstructor createDriver = newDeprecatedRmtDriver;
#else
    extern opentherm::IDriver *newRmtDriver(gpio_num_t rxPin, gpio_num_t txPin);
    DriverConstructor createDriver = newRmtDriver;
#endif

  }  // namespace opentherm

  namespace dev {

    OpenthermSlave::OpenthermSlave(opentherm::IDriver *driver,
                                   opentherm::ISlaveModel *model,
                                   const char *name) {
      opentherm::Slave::init(driver, model, name);
    }

    void OpenthermSlave::setState(const JsonVariantConst state,
                                  DynamicJsonDocument **result) {
      JsonVariantConst hvac = state["hvac"];
      if (hvac) {
        json::set(_hvac.status.value, hvac["status"]);
        json::set(_hvac.tSet, hvac["ts"]);
        json::set(_hvac.coolingControl, hvac["cc"]);
      }
    }

    DynamicJsonDocument *OpenthermSlave::getState(const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(_hvac.jsonSize());
      JsonObject root = doc->to<JsonObject>();
      _hvac.toJson(root);
      return doc;
    }

    bool OpenthermSlave::setConfig(const JsonVariantConst cfg,
                                   DynamicJsonDocument **result) {
      JsonObjectConst hvac = cfg["hvac"];
      bool changed = false;
      if (hvac) {
        _hvac.bounds.fromJson(hvac, &changed);
        json::set(_hvac.config.value, hvac["cfg"], &changed);
        json::set(_hvac.fault.value, hvac["fault"], &changed);
      }
      return changed;
    }

    DynamicJsonDocument *OpenthermSlave::getConfig(
        const JsonVariantConst args) {
      size_t docsize = JSON_OBJECT_SIZE(1 /*hvac*/ + 2 /*tset,config*/) +
                       _hvac.bounds.jsonSize();
      DynamicJsonDocument *doc = new DynamicJsonDocument(docsize);
      JsonObject root = doc->to<JsonObject>();
      auto hvac = root.createNestedObject("hvac");
      json::to(hvac, "ts", _hvac.tSet);
      hvac["config"] = _hvac.config.value;
      _hvac.bounds.toJson(hvac);
      return doc;
    }

    OpenthermMaster::OpenthermMaster(opentherm::IDriver *driver,
                                     opentherm::IMasterModel *model,
                                     const char *name)
        : Device(Flags::HasSensors) {
      opentherm::Master::init(driver, model, name);
    }

    bool OpenthermMaster::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      std::lock_guard lock(_mutexPR);
      if (_pendingResponse) {
        req.respond(ESP_ERR_INVALID_STATE);
        return true;
      }
      esp_err_t err;
      bool processed = false;
      if (req.is("request")) {
        auto data = req.data();
        err = send(
            static_cast<opentherm::MessageType>(data["type"].as<uint8_t>()),
            static_cast<opentherm::DataId>(data["id"].as<uint8_t>()),
            data["value"]);
        processed = true;
      } else if (req.is("scan")) {
        err = setJob(new opentherm::ScanJob());
        processed = true;
      } else if (req.is("snapshot")) {
        err = setJob(new opentherm::SnapshotJob());
        processed = true;
      }
      if (processed) {
        if (err == ESP_OK) {
          _pendingResponse = req.makeResponse();
        } else
          req.respond(err);
      }
      return processed;
    }

    void OpenthermMaster::setState(const JsonVariantConst state,
                                   DynamicJsonDocument **result) {
      JsonVariantConst hvac = state["hvac"];
      if (hvac) {
        json::set(_hvac.status.value, hvac["status"]);
        json::set(_hvac.tSet, hvac["ts"]);
        bool changed = false;
        opentherm::Config c = _hvac.config;
        json::set(c.value, hvac["cfg"], &changed);
        if (changed)
          _hvac.config.master.value = c.master.value;
        json::set(_hvac.coolingControl, hvac["cc"]);
        json::set(_hvac.tSetCh2, hvac["tsch2"]);
        json::set(_hvac.maxRelMod, hvac["maxrm"]);
        json::set(_hvac.trSet, hvac["trs"]);
        json::set(_hvac.trSetCh2, hvac["trsch2"]);
        json::set(_hvac.tr, hvac["tr"]);
        json::set(_hvac.tDhwSet, hvac["tdhws"]);
        json::set(_hvac.maxTSet, hvac["maxts"]);
        json::set(_hvac.hcratio, hvac["hcr"]);
        json::set(_hvac.otverMaster, hvac["otvm"]);
        json::set(_hvac.verMaster, hvac["vm"]);
      }
    }

    DynamicJsonDocument *OpenthermMaster::getState(
        const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(_hvac.jsonSize());
      JsonObject root = doc->to<JsonObject>();
      _hvac.toJson(root);
      return doc;
    }

    bool OpenthermMaster::setConfig(const JsonVariantConst cfg,
                                    DynamicJsonDocument **result) {
      bool changed = false;
      _ids.fromJson(cfg, &changed);
      JsonVariantConst hvac = cfg["hvac"];
      if (hvac) {
        opentherm::Status s = _hvac.status;
        json::set(s.value, hvac["status"], &changed);
        if (changed)
          _hvac.status.master.value = s.master.value;
        json::set(_hvac.tSet, hvac["ts"], &changed);
        json::set(_hvac.tDhwSet, hvac["tdhws"], &changed);
        json::set(_hvac.maxTSet, hvac["maxts"], &changed);
      }
      return changed;
    }

    DynamicJsonDocument *OpenthermMaster::getConfig(
        const JsonVariantConst args) {
      DynamicJsonDocument *doc = new DynamicJsonDocument(
          JSON_OBJECT_SIZE(1 /*hvac*/ + 4) + _ids.jsonSize());
      JsonObject root = doc->to<JsonObject>();
      _ids.toJson(root);
      auto hvac = root.createNestedObject("hvac");
      hvac["status"] = _hvac.status.value & 0xff00;
      json::to(hvac, "ts", _hvac.tSet);
      json::to(hvac, "tdhws", _hvac.tDhwSet);
      json::to(hvac, "maxts", _hvac.maxTSet);
      return doc;
    }

    void OpenthermMaster::processResponse(opentherm::Message &msg) {
      Master::processResponse(msg);
      std::lock_guard lock(_mutexPR);
      if (!_pendingResponse || !_pendingResponse->is("request"))
        return;
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(3));
      auto root = doc->to<JsonObject>();
      root["type"] = static_cast<int>(msg.type);
      root["id"] = static_cast<int>(msg.id);
      root["value"] = msg.value;
      _pendingResponse->setData(doc);
      _pendingResponse->publish();
      delete _pendingResponse;
      _pendingResponse = nullptr;
    }

    void OpenthermMaster::incomingFrameInvalid() {
      std::lock_guard lock(_mutexPR);
      if (!_pendingResponse || !_pendingResponse->is("request"))
        return;
      DynamicJsonDocument *doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(4));
      auto root = doc->to<JsonObject>();
      root["state"] = opentherm::RecvStateNames[static_cast<int>(_recv.state)];
      root["frame"] = _recv.buf;
      root["mbuf"] = _recv.mbuf;
      root["mcnt"] = _recv.mcnt;
      _pendingResponse->setError(doc);
      _pendingResponse->publish();
      delete _pendingResponse;
      _pendingResponse = nullptr;
    }

    void OpenthermMaster::jobDone(opentherm::IMasterModel *job) {
      Master::jobDone(job);
      std::lock_guard lock(_mutexPR);
      const char *name = job->name();
      if (!_pendingResponse || !_pendingResponse->is(name))
        return;
      DynamicJsonDocument *doc = nullptr;
      if (!strcmp(name, "scan")) {
        doc = new DynamicJsonDocument(_ids.jsonSize());
        auto root = doc->to<JsonObject>();
        _ids.toJson(root);
      } else if (!strcmp(name, "snapshot")) {
        auto snapshot = (opentherm::SnapshotJob *)job;
        doc =
            new DynamicJsonDocument(JSON_OBJECT_SIZE(snapshot->values.size()) +
                                    snapshot->values.size() * 4);
        auto root = doc->to<JsonObject>();
        char i[4];
        for (auto const &x : snapshot->values) {
          sprintf(i, "%d", x.first);
          root[i] = x.second;
        }
      }

      if (doc)
        _pendingResponse->setData(doc);
      _pendingResponse->publish();
      delete _pendingResponse;
      _pendingResponse = nullptr;
      EventConfigChanged::publish(this, true);
    }

    bool OpenthermMaster::pollSensors() {
      sensor("tboiler", _hvac.tBoiler);
      sensor("rm", _hvac.relMod);
      sensor("chp", _hvac.chPressure);
      sensor("bfc", _hvac.bfCurrent);
      return true;
    }

    OpenthermMaster *useOpenthermMaster(gpio_num_t rxPin, gpio_num_t txPin,
                                        opentherm::IMasterModel *model,
                                        const char *name) {
      auto driver = opentherm::createDriver(rxPin, txPin);
      if (driver)
        return new OpenthermMaster(driver, model, name);
      return nullptr;
    }

    OpenthermSlave *useOpenthermSlave(gpio_num_t rxPin, gpio_num_t txPin,
                                      opentherm::ISlaveModel *model,
                                      const char *name) {
      auto driver = opentherm::createDriver(rxPin, txPin);
      if (driver)
        return new OpenthermSlave(driver, model, name);
      return nullptr;
    }
  }  // namespace dev
}  // namespace esp32m