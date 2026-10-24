#include "esp32m/debug/rs485.hpp"
#include "esp32m/bus/modbus.hpp"

#include <esp_timer.h>
#include <memory>

namespace esp32m::debug {
  namespace {
    uint64_t nowMs() { return esp_timer_get_time() / 1000ULL; }

    void number(JsonObject object, const char* key, double value) {
      if (std::isfinite(value)) object[key] = value;
      else object[key] = nullptr;
    }
    void counts(JsonObject out, const rs485::Counts& c) {
      out["attempts"] = c.attempts;
      out["success"] = c.success;
      out["timeouts"] = c.timeouts;
      out["invalid"] = c.invalid;
      out["other"] = c.other;
      out["unverified"] = c.unverified;
      out["errors"] = c.errors();
      number(out, "health", c.health());
      number(out, "errorRate", c.percent(c.errors()));
      number(out, "timeoutRate", c.percent(c.timeouts));
      number(out, "invalidRate", c.percent(c.invalid));
      number(out, "otherRate", c.percent(c.other));
      number(out, "meanMs", c.meanMs());
      number(out, "successMs", c.successMs());
      number(out, "maxMs", c.attempts ? c.maxDurationUs / 1000.0 : NAN);
    }
  }

  Rs485& Rs485::instance() {
    static Rs485 instance;
    return instance;
  }

  Rs485::Rs485()
      : _sensors{{this, "", "health"}, {this, "", "health_total"},
                 {this, "", "error_rate"}, {this, "", "timeout_rate"},
                 {this, "", "invalid_rate"}, {this, "", "other_error_rate"},
                 {this, "", "requests_per_second"}, {this, "", "mean_ms"},
                 {this, "", "success_ms"}, {this, "", "max_ms"},
                 {this, "", "attempts_total"}, {this, "", "errors_total"},
                 {this, "", "session"}} {
    const char* titles[] = {
        "RS485 health (15 s)", "RS485 cumulative health",
        "RS485 error rate", "RS485 timeout rate", "RS485 invalid response rate",
        "RS485 other error rate", "RS485 request rate", "RS485 mean duration",
        "RS485 successful response time", "RS485 maximum duration",
        "RS485 total attempts", "RS485 total errors", "RS485 comparison session"};
    for (unsigned i = 0; i < MetricCount; ++i) {
      auto& sensor = _sensors[i];
      sensor.setTitle(titles[i]);
      sensor.stateClass = dev::StateClass::Measurement;
      sensor.precision = i >= AttemptsTotal ? 0 : 2;
      sensor.unit = i <= OtherRate ? "%" : i == RequestRate ? "1/s"
          : i <= MaxMs ? "ms" : nullptr;
    }
    _stats.reset(nowMs());
    Device::init(Flags::None);
    modbus::Master::instance().setObserver(this);
    // Independent of the sequential sensor task: a silent slave must not
    // freeze the metrics describing it. No UART operations in this task.
    const auto created = xTaskCreate([](void* arg) {
      auto* self = static_cast<Rs485*>(arg);
      for (;;) {
        if (App::initialized()) self->publishSensors();
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }, "rs485-health", 6144, this, 1, &_task);
    if (created != pdPASS) {
      _task = nullptr;
      logE("could not start RS485 health sensor publisher");
    }
  }

  void Rs485::prepareTransaction(bool raw, uint8_t address) {
    _stats.prepare(raw, address);
  }

  void Rs485::onTransaction(const modbus::Transaction& transaction) {
    _stats.record(transaction, nowMs());
  }

  void Rs485::publishSensors() {
    std::lock_guard guard(_publishMutex);
    rs485::Summary s;
    _stats.summary(s, nowMs());
    const auto& c = s.recent;
    const double values[] = {
        c.health(), s.total.health(), c.percent(c.errors()),
        c.percent(c.timeouts), c.percent(c.invalid), c.percent(c.other),
        s.windowMs ? c.attempts * 1000.0 / s.windowMs : NAN,
        c.meanMs(), c.successMs(), c.attempts ? c.maxDurationUs / 1000.0 : NAN,
        double(s.total.attempts), double(s.total.errors()), double(s.session)};
    for (unsigned i = 0; i < MetricCount; ++i) {
      if (std::isfinite(values[i])) _sensors[i].set(values[i]);
      else _sensors[i].resetState();
    }
  }

  JsonDocument* Rs485::getState(RequestContext&) {
    // Keep the relatively large per-address snapshot off the UI task stack.
    auto s = std::make_unique<rs485::Snapshot>();
    const auto now = nowMs();
    _stats.snapshot(*s, now);
    auto* doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    root["windowSeconds"] = rs485::WindowSeconds;
    root["windowMs"] = s->windowMs;
    root["windowEndedAtMs"] = s->windowEndMs;
    root["comparisonStartedAtMs"] = s->startedAtMs;
    root["session"] = s->session;
    root["publisherRunning"] = _task != nullptr;
    root["historyBytes"] = s->historyBytes;
    root["lastAttemptAtMs"] = s->total.attempts ? int64_t(s->lastMs) : -1;
    counts(root["recent"].to<JsonObject>(), s->recent);
    counts(root["total"].to<JsonObject>(), s->total);
    number(root, "requestsPerSecond", s->windowMs
        ? s->recent.attempts * 1000.0 / s->windowMs : NAN);
    auto peers = root["peers"].to<JsonArray>();
    for (unsigned i = 0; i < rs485::PeerCount; ++i) {
      const auto& p = s->peers[i];
      if (!p.used) continue;
      auto row = peers.add<JsonObject>();
      row["kind"] = i == rs485::MaxPeers ? "overflow" : p.raw ? "raw" : "modbus";
      row["address"] = p.address;
      row["lastAttemptAtMs"] = int64_t(p.lastMs);
      row["recentAvailable"] = p.recentAvailable;
      if (p.recentAvailable) counts(row["recent"].to<JsonObject>(), p.recent);
      else row["recent"] = nullptr;
      counts(row["total"].to<JsonObject>(), p.total);
    }
    return doc;
  }

  bool Rs485::handleRequest(Request& request) {
    if (AppObject::handleRequest(request)) return true;
    if (!request.is("reset")) return false;
    {
      std::lock_guard guard(_publishMutex);
      _stats.reset(nowMs());
      for (auto& sensor : _sensors) sensor.resetState();
    }
    // This changes diagnostics only, never meter/VFD configuration or traffic.
    request.respond();
    return true;
  }

  Rs485* useRs485() { return &Rs485::instance(); }
}  // namespace esp32m::debug
