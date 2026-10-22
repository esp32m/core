#include "esp32m/dev/flmvalve.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <esp_timer.h>

#include "esp32m/base.hpp"
#include "esp32m/bus/modbus.hpp"
#include "esp32m/defs.hpp"
#include "esp32m/dev/flmvalve/protocol.hpp"
#include "esp32m/integrations/ha/ha.hpp"

namespace esp32m {
  namespace dev {

    namespace {
      namespace p = flmvalve::protocol;

      // This valve's firmware recognizes a frame boundary only after 11 ms of
      // silence. Reserve a little margin so traffic for another slave cannot
      // be concatenated with our first request on a shared bus.
      constexpr uint32_t PollQuietBeforeMs = 15;

      struct VendorReplyContext {
        uint8_t address;
        uint8_t function;
        size_t normalSize;
        p::Reply reply;

        static bool complete(const uint8_t* bytes, size_t size, void* data) {
          auto& context = *static_cast<VendorReplyContext*>(data);
          return p::findReply(bytes, size, context.address, context.function,
                              context.normalSize, context.reply);
        }
      };

      esp_err_t transactVendor(const p::Frame& request, uint8_t replyAddress,
                               uint8_t function, uint8_t* response,
                               size_t responseCapacity, size_t* responseSize) {
        if (!response || !responseCapacity || !responseSize)
          return ESP_ERR_INVALID_ARG;
        uint8_t received[32] = {};
        size_t receivedSize = 0;
        VendorReplyContext context{replyAddress, function, responseCapacity,
                                   {}};
        const auto err = modbus::Master::instance().exchangeRaw(
            request.bytes.data(), request.size, received, sizeof(received),
            &receivedSize, 0, 1500, VendorReplyContext::complete, &context);
        if (err != ESP_OK)
          return err;
        if (!context.reply.size || context.reply.size > responseCapacity)
          return ESP_ERR_INVALID_SIZE;
        memcpy(response, received + context.reply.offset, context.reply.size);
        *responseSize = context.reply.size;
        return ESP_OK;
      }

      esp_err_t validateVendorResponse(const uint8_t* response, size_t size,
                                       uint8_t address, uint8_t function) {
        if (!response || size < 5)
          return ESP_ERR_INVALID_RESPONSE;
        if (!p::validCrc(response, size))
          return ESP_ERR_INVALID_CRC;
        if (response[0] != address)
          return ESP_ERR_INVALID_RESPONSE;
        if (response[1] == (function | 0x80))
          return ESP_ERR_INVALID_RESPONSE;
        return response[1] == function ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
      }
    }  // namespace

    FlomarvelValve::FlomarvelValve(uint8_t addr)
        : _addr(addr),
          _position(this, "", "position"),
          _component(this, "", "valve") {
      _position.setTitle("Actual position");
      _position.unit = "°";
      _position.precision = 2;
      _position.stateClass = StateClass::Measurement;
      _component.setTitle("Valve");
      _component.setReportsPosition(true);
      Device::init(Flags::HasSensors | Flags::CanDisable);
    }

    float FlomarvelValve::rawToAngle(uint16_t raw) {
      return raw / 100.0f;
    }

    int FlomarvelValve::rawToPosition(uint16_t raw) {
      return static_cast<int>(std::lround(raw * 100.0f / MaxAngleRaw));
    }

    uint64_t FlomarvelValve::nowMs() {
      return esp_timer_get_time() / 1000ULL;
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
      _motionStamp = 0;
      _position.resetState();
      _component.resetState();
    }

    void FlomarvelValve::observeActual(uint16_t raw, uint64_t now) {
      const auto movement = raw > _motionRaw ? raw - _motionRaw
                                             : _motionRaw - raw;
      if (!_motionStamp || movement >= MotionThresholdRaw) {
        _motionRaw = raw;
        _motionStamp = now;
      }
      _actualRaw = raw;
      _actualStamp = now;
    }

    void FlomarvelValve::observeTarget(uint16_t raw, uint64_t now) {
      if (!_targetStamp || raw != _targetRaw) {
        _motionRaw = _actualRaw;
        _motionStamp = now;
      }
      _targetRaw = raw;
      _targetStamp = now;
    }

    void FlomarvelValve::updateValveState() {
      if (!_actualStamp) {
        _component.resetState();
        return;
      }
      // Use the latest successful position sample rather than wall time. A
      // communication gap must not make an unobserved movement look settled.
      const bool settled =
          _motionStamp && _actualStamp >= _motionStamp &&
          _actualStamp - _motionStamp >= MotionSettleMs;
      const bool endpointTarget =
          _targetRaw <= EndpointToleranceRaw ||
          _targetRaw >= MaxAngleRaw - EndpointToleranceRaw;
      const auto tolerance =
          endpointTarget ? EndpointToleranceRaw : PositionToleranceRaw;
      ValveState state;
      if (_targetStamp &&
          _targetRaw > _actualRaw + tolerance && !settled)
        state = ValveState::Opening;
      else if (_targetStamp &&
               _actualRaw > _targetRaw + tolerance && !settled)
        state = ValveState::Closing;
      else if (_actualRaw <= EndpointToleranceRaw)
        state = ValveState::Closed;
      else
        state = ValveState::Open;
      _component.set(state, rawToPosition(_actualRaw));
    }

    void FlomarvelValve::invalidateCommunicationState() {
      invalidateActual();
      _targetStamp = 0;
      _probeStamp = 0;
      _probeValidMask = 0;
    }

    void FlomarvelValve::expireStaleState(uint64_t now) {
      if (_actualStamp && now - _actualStamp > StateFreshnessMs)
        invalidateActual();
      if (_targetStamp && now - _targetStamp > StateFreshnessMs)
        _targetStamp = 0;
      updateValveState();
    }

    esp_err_t FlomarvelValve::configureAddress(uint8_t address) {
      if (!address || address > 254)
        return ESP_ERR_INVALID_ARG;

      const auto request = p::configureAddress(_addr, address);
      uint8_t response[7] = {};
      size_t responseSize = 0;
      auto writeError = transactVendor(
          request, address, p::ConfigureAddressFunction, response,
          sizeof(response), &responseSize);
      bool acknowledged = writeError == ESP_OK;
      if (acknowledged) {
        writeError = validateVendorResponse(
            response, responseSize, address, p::ConfigureAddressFunction);
        acknowledged = writeError == ESP_OK && responseSize == sizeof(response) &&
                       response[2] == 0x00 && response[3] == address &&
                       response[4] == 0x02;
        if (!acknowledged && writeError == ESP_OK)
          writeError = ESP_ERR_INVALID_RESPONSE;
      }

      // The command is sent exactly once. A missing acknowledgement is an
      // uncertain result, so verify the requested address read-only instead
      // of retransmitting the configuration write.
      uint16_t actual = 0;
      const auto verifyError = modbus::Master::instance().request(
          address, modbus::Command::ReadHolding, ActualRegister, 1, &actual,
          false);
      if (verifyError == ESP_OK || acknowledged) {
        if (verifyError != ESP_OK)
          logW("address %u acknowledged but read-back failed: %d",
               (unsigned)address, (int)verifyError);
        else if (writeError != ESP_OK)
          logW("address %u verified after missing/invalid acknowledgement: %d",
               (unsigned)address, (int)writeError);
        _addr = address;
        invalidateCommunicationState();
        return ESP_OK;
      }

      logW("address change %u -> %u failed: write=%d verify=%d",
           (unsigned)_addr, (unsigned)address, (int)writeError,
           (int)verifyError);
      return writeError != ESP_OK ? writeError : verifyError;
    }

    esp_err_t FlomarvelValve::configureBaudrate(uint32_t baud) {
      if (!isSupportedBaudrate(baud))
        return ESP_ERR_INVALID_ARG;

      const auto request = p::configureBaudrate(_addr, baud);
      uint8_t response[10] = {};
      size_t responseSize = 0;
      auto writeError = transactVendor(
          request, _addr, p::ConfigureBaudrateFunction, response,
          sizeof(response), &responseSize);
      bool acknowledged = writeError == ESP_OK;
      if (acknowledged) {
        writeError = validateVendorResponse(
            response, responseSize, _addr, p::ConfigureBaudrateFunction);
        acknowledged =
            writeError == ESP_OK && responseSize == sizeof(response) &&
            response[2] == 0x00 && response[3] == 0x00 &&
            response[4] == (baud >> 24) &&
            response[5] == ((baud >> 16) & 0xff) &&
            response[6] == ((baud >> 8) & 0xff) &&
            response[7] == (baud & 0xff);
        if (!acknowledged && writeError == ESP_OK)
          writeError = ESP_ERR_INVALID_RESPONSE;
      }

      auto& mb = modbus::Master::instance();
      const auto previousBaud = mb.config().ser_opts.baudrate;
      ESP_CHECK_RETURN(mb.setBaudrate(baud));
      uint16_t actual = 0;
      const auto verifyError = mb.request(
          _addr, modbus::Command::ReadHolding, ActualRegister, 1, &actual,
          false);
      if (verifyError == ESP_OK || acknowledged) {
        if (verifyError != ESP_OK)
          logW("baud %lu acknowledged but read-back failed: %d",
               (unsigned long)baud, (int)verifyError);
        else if (writeError != ESP_OK)
          logW("baud %lu verified after missing/invalid acknowledgement: %d",
               (unsigned long)baud, (int)writeError);
        invalidateCommunicationState();
        return ESP_OK;
      }

      const auto restoreError = mb.setBaudrate(previousBaud);
      logW("baud change %lu -> %lu failed: write=%d verify=%d restore=%d",
           (unsigned long)previousBaud, (unsigned long)baud, (int)writeError,
           (int)verifyError, (int)restoreError);
      if (restoreError != ESP_OK)
        return restoreError;
      return writeError != ESP_OK ? writeError : verifyError;
    }

    bool FlomarvelValve::initSensors() {
      modbus::Master& mb = modbus::Master::instance();
      return mb.start() == ESP_OK;
    }

    bool FlomarvelValve::pollSensors() {
      modbus::Master& mb = modbus::Master::instance();
      if (!mb.isRunning()) {
        invalidateActual();
        return false;
      }

      uint16_t actual = 0;
      // Keep the quiet interval and transmission under the master's UART
      // mutex. Retain the recovery retry as a fallback for the essential read.
      auto err = mb.request(_addr, modbus::Command::ReadHolding,
                            ActualRegister, 1, &actual, true,
                            PollQuietBeforeMs);
      if (err != ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        expireStaleState(nowMs());
        return true;
      }
      if (actual > MaxAngleRaw) {
        logW("invalid actual angle register value: %u", (unsigned)actual);
        expireStaleState(nowMs());
        return true;
      }
      observeActual(actual, nowMs());
      _position.set(actualAngle());

      uint16_t target = 0;
      err = mb.request(_addr, modbus::Command::ReadHolding, CommandRegister, 1,
                       &target, false);
      if (err == ESP_OK && target <= MaxAngleRaw) {
        observeTarget(target, nowMs());
      } else {
        if (err != ESP_OK)
          ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        else
          logW("invalid commanded angle register value: %u",
               (unsigned)target);
        if (_targetStamp && nowMs() - _targetStamp > StateFreshnessMs)
          _targetStamp = 0;
      }
      updateValveState();
      return true;
    }

    esp_err_t FlomarvelValve::setTargetAngle(float angle) {
      std::lock_guard guard(_deviceMutex);
      if (isDisabled()) return ESP_ERR_INVALID_STATE;
      if (!std::isfinite(angle) || angle < 0 || angle > 90)
        return ESP_ERR_INVALID_ARG;
      modbus::Master& mb = modbus::Master::instance();
      if (!mb.isRunning())
        return ESP_ERR_INVALID_STATE;
      auto raw = static_cast<uint16_t>(std::lround(angle * 100.0f));
      ESP_CHECK_RETURN(mb.request(_addr, modbus::Command::WriteRegister,
                                  CommandRegister, 1, &raw));
      observeTarget(raw, nowMs());
      updateValveState();
      return ESP_OK;
    }

    esp_err_t FlomarvelValve::setPosition(float position) {
      if (!std::isfinite(position) || position < 0 || position > 100)
        return ESP_ERR_INVALID_ARG;
      return setTargetAngle(position * 90.0f / 100.0f);
    }

    esp_err_t FlomarvelValve::open() {
      return setTargetAngle(90);
    }

    esp_err_t FlomarvelValve::close() {
      return setTargetAngle(0);
    }

    esp_err_t FlomarvelValve::probeRegisters() {
      std::lock_guard guard(_deviceMutex);
      if (isDisabled()) return ESP_ERR_INVALID_STATE;
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
      _probeStamp = nowMs();

      if (_probeValidMask & (1 << CommandRegister)) {
        if (_probeRegs[CommandRegister] <= MaxAngleRaw)
          observeTarget(_probeRegs[CommandRegister], _probeStamp);
        else
          _targetStamp = 0;
      }
      if (_probeValidMask & (1 << ActualRegister)) {
        if (_probeRegs[ActualRegister] <= MaxAngleRaw) {
          observeActual(_probeRegs[ActualRegister], _probeStamp);
          _position.set(actualAngle());
        } else {
          invalidateActual();
        }
      }
      updateValveState();

      // Partial results are useful during protocol discovery. Only fail the
      // request if no register answered at all.
      return _probeValidMask ? ESP_OK : firstError;
    }

    bool FlomarvelValve::handleRequest(Request& req) {
      std::lock_guard deviceGuard(_deviceMutex);
      if (Device::handleRequest(req))
        return true;
      if (req.is("probe")) {
        req.respond(probeRegisters());
        return true;
      }
      if (req.is(integrations::ha::StateRequest::Name)) {
        auto id = req.data()["id"].as<const char*>();
        auto component = id ? Component::find(id) : nullptr;
        if (!component || component->device() != this)
          return false;
        JsonDocument doc;
        auto root = doc.to<JsonObject>();
        auto state = root["state"].to<JsonObject>();
        component->exportState(state);
        req.respond(name(), doc.as<JsonVariantConst>());
        return true;
      }
      if (req.is(integrations::ha::CommandRequest::Name)) {
        auto& command =
            static_cast<integrations::ha::CommandRequest&>(req);
        if (command.componentId != _component.id())
          return false;
        esp_err_t err = ESP_ERR_INVALID_ARG;
        auto payload = req.data().as<const char*>();
        if (payload) {
          if (!strcasecmp(payload, "open"))
            err = open();
          else if (!strcasecmp(payload, "close"))
            err = close();
          else {
            char* end = nullptr;
            auto position = strtof(payload, &end);
            if (end != payload && end && *end == '\0')
              err = setPosition(position);
          }
        }
        req.respond(err);
        return true;
      }
      return false;
    }

    void FlomarvelValve::setState(RequestContext& ctx) {
      auto state = ctx.data.as<JsonObjectConst>();
      auto angle = state["angle"];
      auto position = state["position"];
      if (angle.is<float>())
        ctx.errors.check(setTargetAngle(angle.as<float>()));
      else if (position.is<float>())
        ctx.errors.check(setPosition(position.as<float>()));
      else {
        const char* action = state["state"];
        if (!action)
          return;
        esp_err_t err = ESP_ERR_INVALID_ARG;
        if (!strcasecmp(action, "open"))
          err = open();
        else if (!strcasecmp(action, "close"))
          err = close();
        else
          logW("unrecognized valve action: %s", action);
        ctx.errors.check(err);
      }
    }

    bool FlomarvelValve::setDeviceConfig(RequestContext& ctx) {
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

    JsonDocument* FlomarvelValve::getDeviceConfig(RequestContext& ctx) {
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      root["address"] = _addr;
      root["baud"] = modbus::Master::instance().config().ser_opts.baudrate;
      return doc;
    }

    JsonDocument* FlomarvelValve::getState(RequestContext& ctx) {
      auto doc = new JsonDocument();
      auto root = doc->to<JsonObject>();
      const auto now = nowMs();
      const auto actualAge =
          _actualStamp ? now - _actualStamp : uint64_t(0);
      const bool actualFresh =
          _actualStamp && actualAge <= StateFreshnessMs;
      root["address"] = _addr;
      root["actualValid"] = actualFresh;
      if (_actualStamp)
        root["actualSampledAtMs"] = _actualStamp;
      else
        root["actualSampledAtMs"] = -1;
      root["actual"] = actualAngle();
      root["actualRaw"] = _actualRaw;
      root["targetValid"] = _targetStamp != 0;
      if (_targetStamp)
        root["targetSampledAtMs"] = _targetStamp;
      else
        root["targetSampledAtMs"] = -1;
      root["target"] = targetAngle();
      root["targetRaw"] = _targetRaw;
      root["state"] = actualFresh
                            ? _component.getState()["state"] | "unknown"
                            : "unknown";
      root["position"] = actualFresh ? rawToPosition(_actualRaw) : -1;
      if (_probeStamp)
        root["probeSampledAtMs"] = _probeStamp;
      else
        root["probeSampledAtMs"] = -1;
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
