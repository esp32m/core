#include "esp32m/bus/scanner/modbus.hpp"
#include "esp32m/base.hpp"
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <algorithm>

namespace esp32m::bus::scanner {
  namespace {
    uint64_t nowMs() { return esp_timer_get_time() / 1000ULL; }
  }

  bool Modbus::isRead(unsigned cmd) { return cmd >= 1 && cmd <= 4; }
  bool Modbus::isWrite(unsigned cmd) {
    return cmd == 5 || cmd == 6 || cmd == 15 || cmd == 16;
  }
  bool Modbus::isBits(unsigned cmd) {
    return cmd == 1 || cmd == 2 || cmd == 5 || cmd == 15;
  }

  void Modbus::putOptions(JsonObject root, const Options& o) {
    root["from"] = o.from; root["to"] = o.to; root["addr"] = o.addr;
    root["cmd"] = o.cmd; root["regs"] = o.regs; root["regc"] = o.regc;
  }
  void Modbus::putBus(JsonObject root) {
    auto& mb = modbus::Master::instance();
    root["configured"] = mb.isConfigured();
    root["sharedBus"] = true;
    if (!mb.isConfigured()) return;
    const auto& ser = mb.config().ser_opts;
    root["mode"] = ser.mode == MB_ASCII ? "ascii" : "rtu";
    root["uart"] = int(ser.port); root["baud"] = ser.baudrate;
    root["parity"] = int(ser.parity);
  }

  bool Modbus::parse(RequestContext& ctx, Options& o) {
    const auto data = ctx.data;
    if (!data.is<JsonObjectConst>()) {
      ctx.errors.check(ESP_ERR_INVALID_ARG, "Expected an options object");
      return false;
    }
    const auto integer = [&](const char* key, unsigned& value, unsigned min, unsigned max) {
      auto v = data[key];
      if (v.isUnbound()) return true;
      if (!v.is<unsigned>() || v.as<unsigned>() < min || v.as<unsigned>() > max) {
        ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid %s", key);
        return false;
      }
      value = v.as<unsigned>();
      return true;
    };
    if (!integer("from", o.from, 1, 247) || !integer("to", o.to, 1, 247) ||
        !integer("addr", o.addr, 1, 247) || !integer("cmd", o.cmd, 1, 16) ||
        !integer("regs", o.regs, 0, 65535) || !integer("regc", o.regc, 1, MaxValues))
      return false;
    if (o.from > o.to || (!isRead(o.cmd) && !isWrite(o.cmd)) ||
        o.regs + o.regc > 65536 ||
        ((o.cmd == 5 || o.cmd == 6) && o.regc != 1) ||
        (o.cmd == 16 && o.regc > 123)) {
      ctx.errors.check(ESP_ERR_INVALID_ARG, "Invalid range, function or quantity");
      return false;
    }
    // Ignore legacy persisted UART preferences, but reject an interactive
    // attempt to switch the shared bus. No configureSerial()/stop()/start().
    if (!ctx.request.isInternal()) {
      JsonDocument bus;
      putBus(bus.to<JsonObject>());
      for (const auto* key : {"mode", "uart", "baud", "parity"}) {
        if (!data[key].isUnbound() && data[key] != bus[key]) {
          ctx.errors.check(ESP_ERR_INVALID_ARG, "Scanner uses the configured shared bus; %s is read-only", key);
          return false;
        }
      }
    }
    return true;
  }

  JsonDocument* Modbus::getConfig(RequestContext&) {
    std::lock_guard guard(_mutex);
    auto* doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    putOptions(root, _options);
    putBus(root);
    return doc;
  }
  bool Modbus::setConfig(RequestContext& ctx) {
    std::lock_guard guard(_mutex);
    if (_busy) {
      ctx.errors.check(ESP_ERR_INVALID_STATE, "Scanner is busy");
      return false;
    }
    auto options = _options;
    if (!parse(ctx, options)) return false;
    const bool changed = options.from != _options.from || options.to != _options.to ||
        options.addr != _options.addr || options.cmd != _options.cmd ||
        options.regs != _options.regs || options.regc != _options.regc;
    _options = options;
    return changed;
  }

  JsonDocument* Modbus::state() {
    auto* doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    putBus(root);
    putOptions(root, _active);
    root["busy"] = _busy; root["job"] = _jobId;
    root["operation"] = _operation == Operation::Scan ? "scan" :
                        _operation == Operation::Read ? "read" : "write";
    root["scanned"] = _scanned; root["total"] = _total;
    root["progress"] = _total ? 100 * _scanned / _total : 0;
    root["elapsedMs"] = _busy ? nowMs() - _started : _elapsed;
    root["cancelRequested"] = _cancel;
    root["cancelled"] = _cancelled;
    root["error"] = _error;
    auto results = root["results"].to<JsonArray>();
    if (_operation == Operation::Scan) {
      for (unsigned a = _active.from; a < _active.from + _scanned; ++a) {
        auto row = results.add<JsonArray>();
        row.add(a); row.add(_errors[a]);
        if (_errors[a] == ESP_OK) row.add(_first[a]);
      }
    } else {
      auto values = root["values"].to<JsonArray>();
      for (unsigned i = 0; i < _valueCount; ++i) values.add(_values[i]);
      if (_operation == Operation::Write) {
        root["writeError"] = _writeError; root["verifyError"] = _verifyError;
        root["verified"] = _verified;
      }
    }
    return doc;
  }
  JsonDocument* Modbus::getState(RequestContext&) {
    std::lock_guard guard(_mutex);
    return state();
  }

  bool Modbus::handleRequest(Request& request) {
    if (AppObject::handleRequest(request)) return true;
    if (!request.is("scan") && !request.is("request") &&
        !request.is("write") && !request.is("cancel")) return false;
    if (request.isBroadcast()) {
      request.respondError(ESP_ERR_INVALID_ARG, "Target the Modbus scanner explicitly");
      return true;
    }
    if (request.is("cancel")) {
      {
        std::lock_guard guard(_mutex);
        if (_busy && _operation == Operation::Scan) _cancel = true;
      }
      request.respond();
      return true;
    }
    {
      std::lock_guard guard(_mutex);
      if (_busy || !modbus::Master::instance().isConfigured()) {
        request.respondError(ESP_ERR_INVALID_STATE, "Scanner is busy or the shared bus is not ready");
        return true;
      }
      auto job = std::make_unique<Job>();
      job->options = _options;
      RequestContext ctx(request, request.data());
      if (!parse(ctx, job->options)) {
        request.respondError(ESP_ERR_INVALID_ARG, "Invalid scanner options or attempt to change the shared bus");
        return true;
      }
      auto& o = job->options;
      job->operation = request.is("scan") ? Operation::Scan :
                       request.is("write") ? Operation::Write : Operation::Read;
      if ((job->operation != Operation::Write && !isRead(o.cmd)) ||
          (job->operation == Operation::Scan && o.regc != 1) ||
          (job->operation == Operation::Write && !isWrite(o.cmd))) {
        request.respondError(ESP_ERR_INVALID_ARG, "Scan/read require FC01-04; scans probe one value; writes require FC05/06/15/16");
        return true;
      }
      if (job->operation == Operation::Write) {
        auto data = request.data()["data"];
        auto confirm = request.data()["confirm"];
        if (!confirm.is<bool>() || !confirm.as<bool>() ||
            !data.is<JsonArrayConst>() || data.size() != o.regc) {
          request.respondError(ESP_ERR_INVALID_ARG, "Confirm the write and supply exactly one value per coil/register");
          return true;
        }
        for (unsigned i = 0; i < o.regc; ++i) {
          if (!data[i].is<unsigned>() || data[i].as<unsigned>() > (isBits(o.cmd) ? 1U : 65535U)) {
            request.respondError(ESP_ERR_INVALID_ARG, "Invalid coil/register value");
            return true;
          }
          job->values[i] = data[i].as<uint16_t>();
        }
      }
      job->response.reset(request.makeResponse());
      if (!job->response) { request.respond(ESP_ERR_NO_MEM); return true; }
      _pending = std::move(job);
      // Starting under _mutex prevents the new worker from observing a partly
      // initialized job. Retain the previous result/config if creation fails.
      if (!_task &&
          xTaskCreate([](void* self) { static_cast<Modbus*>(self)->run(); },
                      "m/s/modbus", 6144, this, 1, &_task) != pdPASS) {
        _task = nullptr;
        _pending.reset();
        request.respond(ESP_ERR_NO_MEM);
        return true;
      }
      _options = _active = _pending->options;
      _operation = _pending->operation;
      _busy = true; _cancel = _cancelled = _verified = false;
      ++_jobId; _started = nowMs(); _elapsed = 0;
      _scanned = _valueCount = 0;
      _total = _operation == Operation::Scan ? _active.to - _active.from + 1 : 1;
      _error = _writeError = _verifyError = ESP_OK;
    }
    config::Changed::publish(this, true);
    return true;
  }

  void Modbus::publish(Job& job, bool final) {
    JsonDocument* doc;
    {
      std::lock_guard guard(_mutex);
      if (final) {
        _busy = false;
        _cancelled = _cancel && _scanned < _total;
        _elapsed = nowMs() - _started;
      }
      if (final) doc = state();
      else {
        // Keep-alive messages stay small even for a full 247-address scan.
        // The UI polls retained state separately; the final reply has results.
        doc = new JsonDocument();
        (*doc)["job"] = _jobId;
        (*doc)["progress"] = _total ? 100 * _scanned / _total : 0;
        (*doc)["scanned"] = _scanned;
        (*doc)["total"] = _total;
      }
    }
    job.response->setPartial(!final);
    job.response->setData(doc);
    job.response->publish();
  }

  void Modbus::execute(Job& job) {
    const auto& o = job.options;
    auto& mb = modbus::Master::instance();
    std::array<uint16_t, MaxValues> buffer{};
    const auto value = [&](unsigned i) -> uint16_t {
      return isBits(o.cmd) ? (reinterpret_cast<uint8_t*>(buffer.data())[i / 8] >> (i % 8)) & 1 : buffer[i];
    };
    publish(job, false);
    if (job.operation == Operation::Scan) {
      for (unsigned address = o.from; address <= o.to; ++address) {
        { std::lock_guard guard(_mutex); if (_cancel) break; }
        buffer.fill(0);
        auto error = mb.request(address, modbus::Command(o.cmd), o.regs, 1,
                                buffer.data(), false, 100);
        {
          std::lock_guard guard(_mutex);
          _errors[address] = error;
          _first[address] = value(0);
          ++_scanned;
          // Setup failures abort rather than repeating for 247 addresses.
          if (error != ESP_OK && error != ESP_ERR_TIMEOUT &&
              error != ESP_ERR_INVALID_RESPONSE && error != ESP_ERR_NOT_SUPPORTED)
            _error = error;
        }
        esp_task_wdt_reset();
        publish(job, false);
        if (error != ESP_OK && error != ESP_ERR_TIMEOUT &&
            error != ESP_ERR_INVALID_RESPONSE && error != ESP_ERR_NOT_SUPPORTED) break;
        delay(20);  // Let normal pollers acquire the bus between transactions.
      }
    } else {
      if (job.operation == Operation::Write) {
        if (o.cmd == modbus::WriteCoil) buffer[0] = job.values[0] ? 0xff00 : 0;
        else if (o.cmd == modbus::WriteCoils) {
          auto* packed = reinterpret_cast<uint8_t*>(buffer.data());
          for (unsigned i = 0; i < o.regc; ++i)
            if (job.values[i]) packed[i / 8] |= 1 << (i % 8);
        } else buffer = job.values;
        const auto written = mb.request(o.addr, modbus::Command(o.cmd), o.regs,
                                        o.regc, buffer.data(), false, 100);
        { std::lock_guard guard(_mutex); _writeError = written; }
        esp_task_wdt_reset();
        buffer.fill(0);
      }
      const auto command = job.operation == Operation::Write
          ? (isBits(o.cmd) ? modbus::ReadCoils : modbus::ReadHolding)
          : modbus::Command(o.cmd);
      const auto error = mb.request(o.addr, command, o.regs, o.regc,
                                    buffer.data(), false, 100);
      std::lock_guard guard(_mutex);
      _scanned = 1;
      _error = error;
      if (error == ESP_OK) {
        _valueCount = o.regc;
        for (unsigned i = 0; i < o.regc; ++i) _values[i] = value(i);
      }
      if (job.operation == Operation::Write) {
        _verifyError = error;
        _verified = error == ESP_OK && std::equal(job.values.begin(),
                        job.values.begin() + o.regc, _values.begin());
        _error = _verified ? ESP_OK : error != ESP_OK ? error : ESP_ERR_INVALID_RESPONSE;
      }
    }
    publish(job, true);
  }

  void Modbus::run() {
    esp_task_wdt_add(nullptr);
    for (;;) {
      esp_task_wdt_reset();
      std::unique_ptr<Job> job;
      {
        std::lock_guard guard(_mutex);
        job = std::move(_pending);
        if (!job) _task = nullptr;
      }
      if (!job) break;
      execute(*job);
    }
    // No further scanner access after detaching under _mutex. A request either
    // queues work for this worker before detachment or starts its own worker.
    // Notifications are unnecessary, so a retiring task handle cannot be used
    // after deletion (or after FreeRTOS has recycled it for a different task).
    esp_task_wdt_delete(nullptr);
    vTaskDelete(nullptr);
  }
  Modbus& Modbus::instance() { static Modbus instance; return instance; }
  Modbus* useModbus() { return &Modbus::instance(); }
}  // namespace esp32m::bus::scanner
