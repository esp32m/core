#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp32m/dev/dsts.hpp"
// #include "esp32m/integrations/ha/ha.hpp"

namespace esp32m {
  namespace dsts {

    static const uint8_t CmdTempConvert = 0x44;
    static const uint8_t CmdScratchpadWrite = 0x4E;
    static const uint8_t CmdScratchpadRead = 0xBE;
    static const uint8_t CmdScratchpadCopy = 0x48;
    static const uint8_t CmdEepromRecall = 0xB8;
    static const uint8_t CmdPowerSupplyRead = 0xB4;

    static const int TConv =
        750;  // maximum conversion time at 12-bit resolution in milliseconds

    Model Probe::model() const {
      switch (_code.fields.family[0]) {
        case Ds18s20:
        case Ds18b20:
        case Ds1822:
        case Ds1825:
        case Ds28ea00:
          return (Model)_code.fields.family[0];
        default:
          return Model::Unknown;
      }
    }

    bool validate(Resolution r) {
      return (r >= R9b) && (r <= R12b);
    }

    void Probe::setResolution(Resolution v) {
      if (_resolution != v && validate(v)) {
        _resolution = v;
        _modified = true;
      }
    }

    bool Probe::check(esp_err_t err, Core *core, bool resetsFailcount,
                      const char *failmsg) {
      if (err == ESP_OK) {
        if (resetsFailcount)
          _failcount = 0;
        return true;
      }
      if (_error != err) {
        core->logger().logf(log::Level::Warning, "%s: error #%u: %s", _codestr,
                            err, failmsg);
        _error = err;
      }
      if (_failcount != std::numeric_limits<uint8_t>::max() &&
          ++_failcount > core->_maxFailures) {
        core->logger().logf(log::Level::Warning,
                            "%s: too many failures (%u>%u), disabling",
                            _codestr, _failcount, core->_maxFailures);
        _failcount = std::numeric_limits<uint8_t>::max();
        _seen = false;
      }
      _temperature = NAN;
      return false;
    }

    Core::Core(Owb *owb) : _owb(owb) {}

    Probe &Core::add(const owb::ROMCode &addr, bool persistent) {
      auto probe = find(addr);
      if (probe)
        return *probe;
      return _probes.emplace_back(addr, persistent);
    }

    Probe *Core::add(const char *addr, bool persistent) {
      owb::ROMCode code;
      if (!owb::fromString(addr, code))
        return nullptr;
      return &add(code, persistent);
    }

    Probe *Core::find(const owb::ROMCode &addr) {
      for (Probe &p : _probes)
        if (!memcmp(p._code.bytes, addr.bytes, sizeof(owb::ROMCode)))
          return &p;
      return nullptr;
    }

    esp_err_t Core::sync() {
      std::lock_guard guard(_owb->mutex());
      owb::Search search(_owb.get());
      owb::ROMCode *addr;
      while ((addr = search.next())) {
        Probe *found = find(*addr);
        if (!found)
          for (Probe &p : _probes)
            if (p.disabled() && !p._persistent) {
              memcpy(p._code.bytes, addr->bytes, sizeof(owb::ROMCode));
              found->_seen = false;
              found = &p;
              break;
            }
        if (!found)
          found = &add(*addr, false);
        // we need to do full scan first to determine number of devices on the
        // bus
      }
      ESP_CHECK_RETURN(search.error());
      for (Probe &p : _probes) {
        if (!p.disabled()) {
          if (!p.check(readPowerSupply(p), this, false,
                       "reading power supply mode"))
            continue;
          if (p._modified) {
            if (!p.check(writeResolution(p), this, false, "setting resolution"))
              continue;
            p._modified = false;
          }
        }
        if (!p.check(readResolution(p), this, true, "reading resolution"))
          continue;
        if (!p.disabled()) {
          if (!p._seen) {
            logI("found probe %s, resolution %d, %s power", p.codestr(),
                 p._resolution, p._parasite ? "parasite" : "normal");
            p._seen = true;
          }
        }
      }
      _lastSync = millis();
      return ESP_OK;
    }  // namespace dsts

    esp_err_t Core::select(const Probe *probe, bool &present) {
      ESP_CHECK_RETURN(_owb->reset(present));
      if (present) {
        if (_probes.size() == 1 && probe)
          ESP_CHECK_RETURN(_owb->write(owb::RomSkip));
        else {
          ESP_CHECK_RETURN(_owb->write(owb::RomMatch));
          ESP_CHECK_RETURN(_owb->write(probe->_code));
        }
      }
      return ESP_OK;
    }

    esp_err_t Core::select(const Probe *probe) {
      bool present;
      ESP_CHECK_RETURN(select(probe, present));
      return present ? ESP_OK : ESP_ERR_NOT_FOUND;
    }

    esp_err_t Core::readScratchpad(const Probe &probe, Scratchpad &scratchpad) {
      memset(&scratchpad, 0, sizeof(Scratchpad));
      ESP_CHECK_RETURN(select(&probe));
      ESP_CHECK_RETURN(_owb->write(CmdScratchpadRead));
      ESP_CHECK_RETURN(_owb->read(&scratchpad, sizeof(Scratchpad)));
      //             logW("scratchpad %02x%02x%02x%02x%02x%02x%02x%02x%02x",
      //             scratchpad.bytes[0], scratchpad.bytes[1],
      //             scratchpad.bytes[2], scratchpad.bytes[3],
      //             scratchpad.bytes[4], scratchpad.bytes[5],
      //             scratchpad.bytes[6], scratchpad.bytes[7],
      //             scratchpad.bytes[8]);
      bool allZeroes = true;
      for (uint8_t b : scratchpad.bytes)
        if (b) {
          allZeroes = false;
          break;
        }
      if (allZeroes)
        return ESP_ERR_INVALID_RESPONSE;
      if (owb::crc(0, scratchpad.bytes, sizeof(Scratchpad)) != 0)
        return ESP_ERR_INVALID_CRC;
      return ESP_OK;
    }

    esp_err_t Core::writeScratchpad(const Probe &probe,
                                    const Scratchpad &scratchpad, bool verify) {
      ESP_CHECK_RETURN(select(&probe));
      ESP_CHECK_RETURN(_owb->write(CmdScratchpadWrite));
      ESP_CHECK_RETURN(_owb->write(&scratchpad.trigger_high, 3));
      if (verify) {
        Scratchpad s;
        ESP_CHECK_RETURN(readScratchpad(probe, s));
        if (memcmp(&scratchpad.trigger_high, &s.trigger_high, 3) != 0)
          return ESP_ERR_INVALID_RESPONSE;
      }
      return ESP_OK;
    }

    esp_err_t Core::readResolution(Probe &probe) {
      // DS1820 and DS18S20 have no resolution configuration register
      Resolution resolution;
      if (probe.model() == Model::Ds18s20)
        resolution = Resolution::R12b;
      else {
        Scratchpad scratchpad;
        ESP_CHECK_RETURN(readScratchpad(probe, scratchpad));
        Resolution resolution =
            (Resolution)(((scratchpad.configuration >> 5) & 0x03) +
                         Resolution::R9b);
        if (!validate(resolution))
          return ESP_ERR_INVALID_RESPONSE;
      }
      probe._resolution = resolution;
      return ESP_OK;
    }

    esp_err_t Core::writeResolution(Probe &probe) {
      // DS1820 and DS18S20 have no resolution configuration register
      if (probe.model() == Model::Ds18s20)
        return probe._resolution == Resolution::R12b ? ESP_OK
                                                     : ESP_ERR_NOT_SUPPORTED;
      if (!validate(probe._resolution))
        return ESP_ERR_NOT_SUPPORTED;
      Scratchpad scratchpad;
      ESP_CHECK_RETURN(readScratchpad(probe, scratchpad));
      uint8_t value = (((probe._resolution - 1) & 0x03) << 5) | 0x1f;
      if (scratchpad.configuration != value) {
        scratchpad.configuration = value;
        ESP_CHECK_RETURN(writeScratchpad(probe, scratchpad, true));
      }
      return ESP_OK;
    }

    esp_err_t Core::readPowerSupply(Probe &probe) {
      ESP_CHECK_RETURN(select(&probe));
      ESP_CHECK_RETURN(_owb->write(CmdPowerSupplyRead));
      bool bit;
      ESP_CHECK_RETURN(_owb->read(bit));
      probe._parasite = !bit;
      return ESP_OK;
    }

    esp_err_t Core::convert(const Probe &probe) {
      ESP_CHECK_RETURN(select(&probe));
      ESP_CHECK_RETURN(_owb->write(CmdTempConvert));
      return ESP_OK;
    }

    esp_err_t Core::convert() {
      ESP_CHECK_RETURN(select(nullptr));
      ESP_CHECK_RETURN(_owb->write(CmdTempConvert));
      return ESP_OK;
    }

    esp_err_t Core::readTemperature(Probe &probe) {
      if (!validate(probe._resolution))
        return ESP_ERR_NOT_SUPPORTED;
      Scratchpad scratchpad;
      ESP_CHECK_RETURN(readScratchpad(probe, scratchpad));
      int16_t raw =
          (((int16_t)scratchpad.msb) << 11) | (((int16_t)scratchpad.lsb) << 3);
      if ((probe.model() == Model::Ds18s20) && (scratchpad.countPerC != 0))
        raw = ((raw & 0xfff0) << 3) - 32 +
              (((scratchpad.countPerC - scratchpad.countRemain) << 7) /
               scratchpad.countPerC);
      probe.setTemperature((float)raw * 0.0078125);
      return ESP_OK;
    }

    unsigned int Core::conversionTicks(const Probe &probe) {
      int divisor = 1 << (Resolution::R12b - probe._resolution);
      float max_conversion_time = (float)TConv / (float)divisor * 1.1;
      return (unsigned int)(pdMS_TO_TICKS(max_conversion_time));
    }

    esp_err_t Core::waitForConversion(const Probe &probe) {
      auto ticks = conversionTicks(probe);
      if (probe._parasite) {
        vTaskDelay(ticks);
      } else {
        TickType_t start_ticks = xTaskGetTickCount();
        TickType_t duration_ticks = 0;
        bool status = false;
        do {
          vTaskDelay(1);
          ESP_CHECK_RETURN(_owb->read(status));
          duration_ticks = xTaskGetTickCount() - start_ticks;
        } while (status == 0 && duration_ticks < ticks);
        return ESP_OK;
      }
      return ESP_OK;
    }

    bool Core::getTemperature(Probe &probe) {
      std::lock_guard guard(_owb->mutex());
      probe._totalAttempts++;
      if (probe._totalAttempts > 0xfffffff) {
        probe._totalAttempts <<= 1;
        probe._totalSuccess <<= 1;
      }
      if (!probe.check(convert(probe), this, false, "convert"))
        return false;
      if (!probe.check(waitForConversion(probe), this, false,
                       "wait for conversion"))
        return false;
      if (!probe.check(readTemperature(probe), this, true, "read temperature"))
        return false;
      return true;
    }

    std::vector<Probe> &Core::probes() {
      if (millis() - _lastSync > 3000)
        sync();
      return _probes;
    }

  }  // namespace dsts

  namespace dev {

    Dsts::Dsts(Owb *owb) : dsts::Core(owb) {
      Device::init(Flags::HasSensors);
      _sensorGroup = sensor::nextGroup();
    };

    Sensor &Dsts::getSensor(const dsts::Probe &probe) {
      std::string id("_");
      id += probe.codestr();
      auto sensor = sensor::find(this, id.c_str());
      if (!sensor) {
        std::string id(
            "_");  // prefix because HA doesn't like ids starting with numbers
        id += probe.codestr();
        sensor = new Sensor(this, "temperature", id.c_str());
        sensor->precision = 2;
        sensor->group = _sensorGroup;
        if (probe.name)
          sensor->name = probe.name;
        auto props = new JsonDocument(
            /*JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(strlen(probe.codestr()))*/);
        auto root = props->to<JsonObject>();
        root["code"] =
            (char *)
                probe.codestr();  // copy string in case sensor outlives probe
        sensor->setProps(props);
      }
      return *sensor;
    }

    bool Dsts::pollSensors() {
      bool changed = false;
      for (dsts::Probe &p : probes())
        if (!p.disabled()) {
          if (getTemperature(p)) {
            float t = p.temperature();
            if (!isnan(t)) {
              auto &s = getSensor(p);
              s.set(t, &changed);
            }
          }
        }
      if (changed)
        sensor::GroupChanged::publish(_sensorGroup);
      return true;
    }

    JsonDocument *Dsts::getState(RequestContext &ctx) {
      auto &pv = probes();
      JsonDocument *doc = new JsonDocument(
          /*JSON_ARRAY_SIZE(pv.size()) + JSON_ARRAY_SIZE(6) * pv.size()*/);
      JsonArray root = doc->to<JsonArray>();
      for (auto &p : pv) {
        auto entry = root.add<JsonArray>();
        entry.add(p.codestr());
        entry.add(p.getResolution());
        entry.add(p.disabled());
        entry.add(p.temperature());
        entry.add(p.successRate());
        if (p.name)
          entry.add(p.name);
      }
      return doc;
    }

    bool Dsts::initSensors() {
      return sync() == ESP_OK;
    }

    Dsts *useDsts(gpio_num_t pin) {
      return new Dsts(new Owb(pin));
    }
  }  // namespace dev
}  // namespace esp32m