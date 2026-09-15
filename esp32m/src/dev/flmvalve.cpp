#include "esp32m/dev/flmvalve.hpp"

#include <cmath>
#include <cstring>

#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"

namespace esp32m {
  namespace dev {

    namespace {
      constexpr uint8_t ConfigureAddressFunction = 0x20;
      constexpr uint8_t ConfigureBaudrateFunction = 0x30;

      uint16_t modbusCrc16(const uint8_t* data, size_t size) {
        uint16_t crc = 0xffff;
        while (size--) {
          crc ^= *data++;
          for (int bit = 0; bit < 8; bit++)
            crc = crc & 1 ? (crc >> 1) ^ 0xa001 : crc >> 1;
        }
        return crc;
      }

      esp_err_t transactVendor(uint8_t address, uint8_t function,
                               const uint8_t* payload, size_t payloadSize,
                               uint8_t* response, size_t responseCapacity,
                               size_t* responseSize) {
        uint8_t request[16] = {};
        const size_t requestSize = payloadSize + 4;
        if (!payload || requestSize > sizeof(request))
          return ESP_ERR_INVALID_ARG;
        request[0] = address;
        request[1] = function;
        memcpy(request + 2, payload, payloadSize);
        const auto crc = modbusCrc16(request, requestSize - 2);
        request[requestSize - 2] = crc & 0xff;
        request[requestSize - 1] = crc >> 8;
        return modbus::Master::instance().transactRaw(
            request, requestSize, response, responseCapacity, responseSize);
      }

      esp_err_t validateVendorResponse(const uint8_t* response, size_t size,
                                       uint8_t address, uint8_t function) {
        if (!response || size < 5)
          return ESP_ERR_INVALID_RESPONSE;
        const auto crc = modbusCrc16(response, size - 2);
        if (response[size - 2] != (crc & 0xff) ||
            response[size - 1] != (crc >> 8))
          return ESP_ERR_INVALID_CRC;
        if (response[0] != address)
          return ESP_ERR_INVALID_RESPONSE;
        if (response[1] == (function | 0x80))
          return ESP_ERR_INVALID_RESPONSE;
        return response[1] == function ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
      }
    }  // namespace

    FlomarvelValve::FlomarvelValve(uint8_t addr)
        : _addr(addr), _position(this, "", "position") {
      _position.setTitle("Actual position");
      _position.unit = "°";
      _position.precision = 2;
      _position.stateClass = StateClass::Measurement;
      Device::init(Flags::HasSensors);
    }

    float FlomarvelValve::rawToAngle(uint16_t raw) {
      return raw / 100.0f;
    }

    bool FlomarvelValve::isSupportedBaudrate(uint32_t baud) {
      switch (baud) {
        case 1200:
        case 2400:
        case 4800:
        case 9600:
        case 19200:
        case 38400:
        case 57600:
        case 115200:
          return true;
        default:
          return false;
      }
    }

    float FlomarvelValve::targetAngle() const {
      return rawToAngle(_targetRaw);
    }

    float FlomarvelValve::actualAngle() const {
      return rawToAngle(_actualRaw);
    }

    void FlomarvelValve::invalidateActual() {
      _actualStamp = 0;
      _position.resetState();
    }

    void FlomarvelValve::invalidateCommunicationState() {
      invalidateActual();
      _targetStamp = 0;
      _probeStamp = 0;
      _probeValidMask = 0;
    }

    esp_err_t FlomarvelValve::configureAddress(uint8_t address) {
      if (!address || address > 254)
        return ESP_ERR_INVALID_ARG;

      const uint8_t payload[] = {0x00, 0x20, 0x00, 0x01,
                                 0x02, 0x00, address};
      uint8_t response[7] = {};
      size_t responseSize = 0;
      ESP_CHECK_RETURN(transactVendor(
          _addr, ConfigureAddressFunction, payload, sizeof(payload), response,
          sizeof(response), &responseSize));
      ESP_CHECK_RETURN(validateVendorResponse(
          response, responseSize, address, ConfigureAddressFunction));
      if (responseSize != sizeof(response) || response[2] != 0x00 ||
          response[3] != address || response[4] != 0x02)
        return ESP_ERR_INVALID_RESPONSE;

      _addr = address;
      invalidateCommunicationState();
      delay(50);
      return ESP_OK;
    }

    esp_err_t FlomarvelValve::configureBaudrate(uint32_t baud) {
      if (!isSupportedBaudrate(baud))
        return ESP_ERR_INVALID_ARG;

      const uint8_t payload[] = {
          0x00, 0x30, 0x00, 0x01, 0x04,
          static_cast<uint8_t>(baud >> 24),
          static_cast<uint8_t>(baud >> 16),
          static_cast<uint8_t>(baud >> 8),
          static_cast<uint8_t>(baud),
      };
      uint8_t response[10] = {};
      size_t responseSize = 0;
      ESP_CHECK_RETURN(transactVendor(
          _addr, ConfigureBaudrateFunction, payload, sizeof(payload), response,
          sizeof(response), &responseSize));
      ESP_CHECK_RETURN(validateVendorResponse(
          response, responseSize, _addr, ConfigureBaudrateFunction));
      if (responseSize != sizeof(response) || response[2] != 0x00 ||
          response[3] != 0x00 || response[4] != (baud >> 24) ||
          response[5] != ((baud >> 16) & 0xff) ||
          response[6] != ((baud >> 8) & 0xff) ||
          response[7] != (baud & 0xff))
        return ESP_ERR_INVALID_RESPONSE;

      delay(50);
      ESP_CHECK_RETURN(modbus::Master::instance().setBaudrate(baud));
      invalidateCommunicationState();
      return ESP_OK;
    }

    bool FlomarvelValve::initSensors() {
      modbus::Master& mb = modbus::Master::instance();
      if (mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.stop());
      delay(500);
      if (!mb.isRunning())
        ESP_ERROR_CHECK_WITHOUT_ABORT(mb.start());
      delay(500);
      return mb.isRunning();
    }

    bool FlomarvelValve::pollSensors() {
      modbus::Master& mb = modbus::Master::instance();
      if (!mb.isRunning()) {
        invalidateActual();
        return false;
      }

      uint16_t actual = 0;
      auto err = mb.request(_addr, modbus::Command::ReadHolding,
                            ActualRegister, 1, &actual);
      if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        invalidateActual();
        return false;
      }
      if (actual > MaxAngleRaw) {
        logW("invalid actual angle register value: %u", (unsigned)actual);
        invalidateActual();
        return false;
      }
      _actualRaw = actual;
      _actualStamp = millis();
      _position.set(actualAngle());

      uint16_t target = 0;
      err = mb.request(_addr, modbus::Command::ReadHolding, CommandRegister, 1,
                       &target);
      if (err == ESP_OK && target <= MaxAngleRaw) {
        _targetRaw = target;
        _targetStamp = millis();
      } else {
        _targetStamp = 0;
        if (err != ESP_OK)
          ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        else
          logW("invalid commanded angle register value: %u",
               (unsigned)target);
      }
      return true;
    }

    esp_err_t FlomarvelValve::setTargetAngle(float angle) {
      if (!std::isfinite(angle) || angle < 0 || angle > 90)
        return ESP_ERR_INVALID_ARG;
      modbus::Master& mb = modbus::Master::instance();
      if (!mb.isRunning())
        return ESP_ERR_INVALID_STATE;
      auto raw = static_cast<uint16_t>(std::lround(angle * 100.0f));
      ESP_CHECK_RETURN(mb.request(_addr, modbus::Command::WriteRegister,
                                  CommandRegister, 1, &raw));
      _targetRaw = raw;
      _targetStamp = millis();
      return ESP_OK;
    }

    esp_err_t FlomarvelValve::probeRegisters() {
      modbus::Master& mb = modbus::Master::instance();
      if (!mb.isRunning())
        return ESP_ERR_INVALID_STATE;

      _probeValidMask = 0;
      esp_err_t firstError = ESP_OK;
      for (uint16_t reg = 0; reg < 4; reg++) {
        uint16_t value = 0;
        auto err = mb.request(_addr, modbus::Command::ReadHolding, reg, 1,
                              &value);
        if (err == ESP_OK) {
          _probeRegs[reg] = value;
          _probeValidMask |= 1 << reg;
          logI("probe R%u = %u (0x%04X)", (unsigned)reg, (unsigned)value,
               (unsigned)value);
        } else {
          if (firstError == ESP_OK)
            firstError = err;
          logW("probe R%u failed: %d", (unsigned)reg, (int)err);
        }
      }
      _probeStamp = millis();

      if (_probeValidMask & (1 << CommandRegister)) {
        _targetRaw = _probeRegs[CommandRegister];
        _targetStamp = _targetRaw <= MaxAngleRaw ? _probeStamp : 0;
      }
      if (_probeValidMask & (1 << ActualRegister)) {
        _actualRaw = _probeRegs[ActualRegister];
        if (_actualRaw <= MaxAngleRaw) {
          _actualStamp = _probeStamp;
          _position.set(actualAngle());
        } else {
          invalidateActual();
        }
      }

      // Partial results are useful during protocol discovery. Only fail the
      // request if no register answered at all.
      return _probeValidMask ? ESP_OK : firstError;
    }

    bool FlomarvelValve::handleRequest(Request& req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("probe")) {
        req.respond(probeRegisters());
        return true;
      }
      return false;
    }

    void FlomarvelValve::setState(RequestContext& ctx) {
      auto state = ctx.data.as<JsonObjectConst>();
      auto value = state["angle"];
      if (value.is<float>())
        ctx.errors.check(setTargetAngle(value.as<float>()));
    }

    bool FlomarvelValve::setConfig(RequestContext& ctx) {
      auto cfg = ctx.data.as<JsonObjectConst>();
      auto& mb = modbus::Master::instance();
      uint8_t address = _addr;
      uint32_t baud = mb.config().ser_opts.baudrate;

      auto addressValue = cfg["address"];
      if (!addressValue.isUnbound()) {
        if (!addressValue.is<int>() || addressValue.as<int>() < 1 ||
            addressValue.as<int>() > 254) {
          ctx.errors.check(ESP_ERR_INVALID_ARG,
                           "Modbus address must be between 1 and 254");
          return false;
        }
        address = addressValue.as<uint8_t>();
      }

      auto baudValue = cfg["baud"];
      if (!baudValue.isUnbound()) {
        if (!baudValue.is<uint32_t>() ||
            !isSupportedBaudrate(baudValue.as<uint32_t>())) {
          ctx.errors.check(ESP_ERR_INVALID_ARG,
                           "Unsupported Modbus baud rate");
          return false;
        }
        baud = baudValue.as<uint32_t>();
      }

      bool changed = false;
      if (ctx.request.isInternal()) {
        if (_addr != address) {
          _addr = address;
          invalidateCommunicationState();
          changed = true;
        }
        if (mb.config().ser_opts.baudrate != baud) {
          auto err = mb.setBaudrate(baud);
          ctx.errors.check(err, "Failed to restore Modbus baud rate");
          if (err == ESP_OK) {
            invalidateCommunicationState();
            changed = true;
          }
        }
        return changed;
      }

      if (_addr != address) {
        auto err = configureAddress(address);
        ctx.errors.check(err, "Failed to change valve Modbus address");
        if (err != ESP_OK)
          return changed;
        changed = true;
      }
      if (mb.config().ser_opts.baudrate != baud) {
        auto err = configureBaudrate(baud);
        ctx.errors.check(err, "Failed to change valve baud rate");
        if (err != ESP_OK)
          return changed;
        changed = true;
      }
      return changed;
    }

    JsonDocument* FlomarvelValve::getConfig(RequestContext& ctx) {
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      root["address"] = _addr;
      root["baud"] = modbus::Master::instance().config().ser_opts.baudrate;
      return doc;
    }

    JsonDocument* FlomarvelValve::getState(RequestContext& ctx) {
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      root["address"] = _addr;
      root["actualValid"] = _actualStamp != 0;
      if (_actualStamp)
        root["actualAge"] = millis() - _actualStamp;
      else
        root["actualAge"] = -1;
      root["actual"] = actualAngle();
      root["actualRaw"] = _actualRaw;
      root["targetValid"] = _targetStamp != 0;
      if (_targetStamp)
        root["targetAge"] = millis() - _targetStamp;
      else
        root["targetAge"] = -1;
      root["target"] = targetAngle();
      root["targetRaw"] = _targetRaw;
      if (_probeStamp)
        root["probeAge"] = millis() - _probeStamp;
      else
        root["probeAge"] = -1;
      root["probeValidMask"] = _probeValidMask;
      auto registers = root["probeRegisters"].to<JsonArray>();
      for (auto value : _probeRegs)
        registers.add(value);
      return doc;
    }

    FlomarvelValve* useFlomarvelValve(uint8_t addr) {
      return new FlomarvelValve(addr);
    }

  }  // namespace dev
}  // namespace esp32m
