#include "esp32m/debug/opentherm.hpp"

namespace esp32m {
  namespace debug {

    /* ---- One-shot write job used for UI-triggered register writes ---- */

    class WriteJob : public opentherm::IMasterModel {
     public:
      WriteJob(uint8_t id, uint16_t value) : _id(id), _value(value) {
        _commDelay = opentherm::MinCommDelay;
      }
      const char* name() const override {
        return "otm-dbg-write";
      }
      bool nextRequest(opentherm::Message& msg) override {
        if (_sent)
          return false;
        msg.write(static_cast<opentherm::DataId>(_id), _value);
        _sent = true;
        return true;
      }
      bool processResponse(opentherm::Message& msg) override {
        return false;  // let Master::processResponse() update _hvac / _ids
      }

     private:
      uint8_t _id;
      uint16_t _value;
      bool _sent = false;
    };

    /* ---- Polling model that cycles through the configured register list ---- */

    class OpenthermMaster::PollModel : public opentherm::IMasterModel {
     public:
      explicit PollModel(OpenthermMaster* owner) : _owner(owner) {
        _commDelay = opentherm::MinCommDelay;
      }
      const char* name() const override {
        return "otm-dbg-poll";
      }
      bool nextRequest(opentherm::Message& msg) override {
        const auto& ids = _owner->_pollIds;
        if (ids.empty())
          return false;
        _index = _index % ids.size();
        msg.read(static_cast<opentherm::DataId>(ids[_index]));
        _index++;
        return true;
      }
      bool processResponse(opentherm::Message& msg) override {
        return false;  // let Master::processResponse() handle _hvac updates
      }

     private:
      OpenthermMaster* _owner;
      size_t _index = 0;
    };

    /* ---- Default poll list (most commonly useful registers) ---- */

    const std::vector<uint8_t>& OpenthermMaster::defaultPollIds() {
      using DId = opentherm::DataId;
      static const std::vector<uint8_t> ids = {
          static_cast<uint8_t>(DId::Status),
          static_cast<uint8_t>(DId::TSet),
          static_cast<uint8_t>(DId::ASFflags),
          static_cast<uint8_t>(DId::RBPflags),
          static_cast<uint8_t>(DId::MaxRelModLevelSetting),
          static_cast<uint8_t>(DId::MaxCapacityMinModLevel),
          static_cast<uint8_t>(DId::RelModLevel),
          static_cast<uint8_t>(DId::CHPressure),
          static_cast<uint8_t>(DId::Tboiler),
          static_cast<uint8_t>(DId::Tdhw),
          static_cast<uint8_t>(DId::Toutside),
          static_cast<uint8_t>(DId::Tret),
          static_cast<uint8_t>(DId::TdhwSetUBTdhwSetLB),
          static_cast<uint8_t>(DId::MaxTSetUBMaxTSetLB),
          static_cast<uint8_t>(DId::TdhwSet),
          static_cast<uint8_t>(DId::MaxTSet),
          static_cast<uint8_t>(DId::BurnerStarts),
          static_cast<uint8_t>(DId::BurnerOperationHours),
          static_cast<uint8_t>(DId::OpenThermVersionSlave),
          static_cast<uint8_t>(DId::SlaveVersion),
      };
      return ids;
    }

    /* ---- OpenthermMaster implementation ---- */

    OpenthermMaster::OpenthermMaster(opentherm::IDriver* driver,
                                     const char* name) {
      _pollIds = defaultPollIds();
      opentherm::Master::init(driver, new PollModel(this),
                              name ? name : "otm-debug");
    }

    bool OpenthermMaster::handleRequest(Request& req) {
      if (AppObject::handleRequest(req))
        return true;
      std::lock_guard<std::mutex> lock(_mutexPR);
      if (_pendingResponse) {
        req.respond(ESP_ERR_INVALID_STATE);
        return true;
      }
      if (req.is("write")) {
        auto data = req.data();
        uint8_t id = data["id"].as<uint8_t>();
        uint16_t value = data["value"].as<uint16_t>();
        auto err = setJob(new WriteJob(id, value));
        if (err == ESP_OK) {
          _pendingResponse = req.makeResponse();
        } else {
          req.respond(err);
        }
        return true;
      }
      return false;
    }

    JsonDocument* OpenthermMaster::getState(RequestContext& ctx) {
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      auto regs = root["regs"].to<JsonObject>();
      std::lock_guard<std::mutex> lock(_snapshotMutex);
      char key[4];
      for (auto& [id, val] : _snapshot) {
        snprintf(key, sizeof(key), "%d", id);
        regs[key] = val;
      }
      return doc;
    }

    bool OpenthermMaster::setConfig(RequestContext& ctx) {
      auto idsJson = ctx.data["ids"].as<JsonArrayConst>();
      if (!idsJson)
        return false;
      std::vector<uint8_t> newIds;
      newIds.reserve(idsJson.size());
      for (JsonVariantConst v : idsJson)
        newIds.push_back(v.as<uint8_t>());
      if (newIds == _pollIds)
        return false;
      _pollIds = std::move(newIds);
      return true;
    }

    JsonDocument* OpenthermMaster::getConfig(RequestContext& ctx) {
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      auto ids = root["ids"].to<JsonArray>();
      for (auto id : _pollIds)
        ids.add(id);
      return doc;
    }

    void OpenthermMaster::processResponse(opentherm::Message& msg) {
      Master::processResponse(msg);
      if (msg.type == opentherm::MessageType::ReadAck ||
          msg.type == opentherm::MessageType::WriteAck) {
        std::lock_guard<std::mutex> lock(_snapshotMutex);
        _snapshot[static_cast<uint8_t>(msg.id)] = msg.value;
      }
      std::lock_guard<std::mutex> lock(_mutexPR);
      if (!_pendingResponse)
        return;
      auto doc = new JsonDocument();
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
      std::lock_guard<std::mutex> lock(_mutexPR);
      if (!_pendingResponse)
        return;
      auto doc = new JsonDocument();
      doc->set("invalid frame");
      _pendingResponse->setError(doc);
      _pendingResponse->publish();
      delete _pendingResponse;
      _pendingResponse = nullptr;
    }

    OpenthermMaster* useOpenthermMaster(gpio_num_t rx, gpio_num_t tx) {
      auto driver = opentherm::newDriver(rx, tx);
      if (!driver)
        return nullptr;
      return new OpenthermMaster(driver);
    }

  }  // namespace debug
}  // namespace esp32m
