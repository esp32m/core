#include "esp32m/log/store.hpp"

#include "esp32m/net/ota.hpp"
#include "esp32m/net/sntp.hpp"

#include <esp_random.h>
#include <esp_timer.h>
#include <nvs.h>
#include <lwip/sockets.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>

namespace esp32m::log {
  namespace {
    int64_t monotonicMs() { return esp_timer_get_time() / 1000; }
    int64_t utcMs() {
      timeval now{};
      gettimeofday(&now, nullptr);
      return int64_t(now.tv_sec) * 1000 + now.tv_usec / 1000;
    }
    std::string randomId() {
      char text[17];
      snprintf(text, sizeof(text), "%08lx%08lx",
               static_cast<unsigned long>(esp_random()),
               static_cast<unsigned long>(esp_random()));
      return text;
    }
    // Preserve byte cursors while avoiding partial UTF-8 code points in JSON.
    void utf8Page(storage::Read& page) {
      size_t first = 0, end = page.text.size();
      while (first < end &&
             (static_cast<unsigned char>(page.text[first]) & 0xc0) == 0x80)
        ++first;
      if (end > first && !page.eof) {
        size_t last = end - 1;
        while (last > first &&
               (static_cast<unsigned char>(page.text[last]) & 0xc0) == 0x80)
          --last;
        const auto lead = static_cast<unsigned char>(page.text[last]);
        const size_t count = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2
            : (lead & 0xf0) == 0xe0 ? 3 : (lead & 0xf8) == 0xf0 ? 4 : 1;
        if (end - last < count) end = last;
      }
      page.offset += first;
      page.nextOffset = page.offset + end - first;
      page.text = page.text.substr(first, end - first);
      // UART/network-originated messages can contain malformed bytes. Keep the
      // WebSocket frame valid UTF-8 without changing raw exported file bytes or
      // expanding the bounded page. Replacement uses one ASCII '?' per bad byte.
      for (size_t i = 0; i < page.text.size();) {
        const auto lead = static_cast<unsigned char>(page.text[i]);
        if (lead < 0x80) { ++i; continue; }
        const size_t count = lead >= 0xc2 && lead <= 0xdf ? 2 :
            lead >= 0xe0 && lead <= 0xef ? 3 :
            lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        bool valid = count && i + count <= page.text.size();
        for (size_t j = 1; valid && j < count; ++j)
          valid = (static_cast<unsigned char>(page.text[i + j]) & 0xc0) == 0x80;
        if (valid && count >= 3) {
          const auto second = static_cast<unsigned char>(page.text[i + 1]);
          valid = !(lead == 0xe0 && second < 0xa0) &&
              !(lead == 0xed && second >= 0xa0) &&
              !(lead == 0xf0 && second < 0x90) &&
              !(lead == 0xf4 && second >= 0x90);
        }
        if (valid) i += count;
        else page.text[i++] = '?';
      }
      page.eof = page.nextOffset >= page.size;
    }
  }

  struct Store::Export {
    Store* owner;
    httpd_req_t* request;
    storage::Snapshot snapshot;
  };

  Store& Store::instance() {
    static Store store;
    return store;
  }

  Store::Store()
      : ui::IHttpHandler("/logs/download", HTTP_GET),
        _engine("/", [this]() { return reserveId(); }), _bootId(randomId()) {
    addAppender(this);
    fs::setRootFormatHooks([this]() { return beforeFormat(); },
                           [this](bool ok) { afterFormat(ok); });
  }

  uint64_t Store::reserveId() {
    nvs_handle_t handle;
    if (nvs_open("m-logs", NVS_READWRITE, &handle) != ESP_OK) return 0;
    uint64_t next = 0;
    auto error = nvs_get_u64(handle, "next", &next);
    const auto floor = _engine.files().empty() ? uint64_t(0) :
        strtoull(_engine.files().back().id.c_str(), nullptr, 16);
    if (floor == std::numeric_limits<uint64_t>::max()) {
      nvs_close(handle);
      return 0;
    }
    if (error == ESP_ERR_NVS_NOT_FOUND) {
      // Random epoch when NVS was reset; durable increasing IDs thereafter.
      next = floor ? floor + 1 :
          ((uint64_t(esp_random()) << 32) | esp_random()) >> 1;
      if (!next) next = 1;
      error = ESP_OK;
    }
    if (next <= floor && floor != std::numeric_limits<uint64_t>::max())
      next = floor + 1;
    const uint64_t reserved = next;
    if (error == ESP_OK && next != std::numeric_limits<uint64_t>::max()) {
      error = nvs_set_u64(handle, "next", next + 1);
      if (error == ESP_OK) error = nvs_commit(handle);
    } else error = ESP_FAIL;
    nvs_close(handle);
    return error == ESP_OK ? reserved : 0;
  }

  size_t Store::reserveLocked() const {
    return _reserveBytes;
  }

  size_t Store::quotaBaseLocked() const {
    // The engine adjusts its capacity accounting as it writes/prunes. Mixing
    // its new owned size with the pre-operation filesystem sample would make
    // the displayed percentage drift until the next physical capacity query.
    if (_ready) return _engine.status().quotaBaseBytes;
    return storage::Engine::quotaBase({_root.total, _root.used, _reserveBytes},
                                       _engine.status().usedBytes);
  }

  size_t Store::quotaBudgetLocked() const {
    return storage::Engine::quotaBudget(_policy, {quotaBaseLocked(), 0, 0}, 0);
  }

  storage::Capacity Store::capacityLocked() {
    const auto previous = _root;
    const auto previousReserve = _reserveBytes;
    _root = fs::rootInfo();
    struct stat primary{}, backup{};
    stat("/config.json", &primary);
    stat("/config.json.bak", &backup);
    const auto configBytes = std::max<size_t>(4096,
        std::max(primary.st_size > 0 ? size_t(primary.st_size) : 0,
                 backup.st_size > 0 ? size_t(backup.st_size) : 0));
    _reserveBytes = std::max<size_t>({_root.total / 10, size_t(16384),
                                     configBytes * 3 + 8192});
    if (_root.ready != previous.ready) markStateChanged(true);
    else if (_root.total != previous.total || _root.used != previous.used ||
             _root.backend != previous.backend || _root.label != previous.label ||
             _reserveBytes != previousReserve) markStateChanged();
    return {_root.total, _root.used, _reserveBytes};
  }

  void Store::prepare() {
    std::lock_guard guard(_mutex);
    capacityLocked();
    _prepared = true;
  }

  void Store::recordErrorLocked(const std::string& error) {
    if (_error == error) return;
    _error = error;  // Never log an appender failure back into the failing sink.
    markStateChanged(true);
  }

  bool Store::applyLocked(const storage::Result& result, bool clearError) {
    if (!result) {
      recordErrorLocked(result.error);
      if (result.pressure) cancelExportLocked();
      return false;
    }
    // Individual successful I/O steps do not resolve a service-level failure
    // such as unavailable capacity or an unusable budget. Only a complete
    // healthy maintenance outcome is allowed to clear the displayed error.
    if (clearError && !_error.empty()) {
      _error.clear();
      markStateChanged(true);
    }
    return true;
  }

  void Store::markStateChanged(bool urgent) {
    _stateDirty.fetch_or(urgent ? 2 : 1);
    // Producers must not read a worker handle: an idle worker can retire while
    // the caller is preempted. Its 100 ms wait or EventPeriodic publishes later.
  }

  void Store::publishStateChanged() {
    // This helper is called only after releasing _mutex. Event consumers can
    // therefore inspect cached state safely even if they capture synchronously.
    if (_publishingState.test_and_set()) return;
    struct PublishingDone {
      std::atomic_flag& flag;
      ~PublishingDone() { flag.clear(); }
    } done{_publishingState};
    const auto dirty = _stateDirty.load();
    const auto now = monotonicMs();
    if (!dirty || (!(dirty & 2) && now < _nextStatePublish)) return;
    if (!_stateDirty.exchange(0)) return;
    _nextStatePublish = now + 1000;
    // Normal AppObject invalidation: the UI coordinator captures later, shares
    // one baseline across subscribers, and suppresses equal state documents.
    EventStateChanged::publish(this);
  }

  uint32_t Store::stateFlagsLocked() const {
    return (_root.ready ? 1u : 0u) | (_ready ? 2u : 0u) |
        (_capture.load() ? 4u : 0u) | (_suspended.load() ? 8u : 0u) |
        (_otaPaused ? 16u : 0u) | (_enabled.load() ? 32u : 0u) |
        (_exportActive.load() ? 64u : 0u) | (_timeValid ? 128u : 0u);
  }

  void Store::catalogLocked() {
    std::string signature;
    for (const auto& file : _engine.files())
      signature += file.id + (file.active ? "+" : "-");
    if (_generation.empty() || signature != _catalogSignature) {
      _catalogSignature = std::move(signature);
      _generation = _bootId + "-" + std::to_string(++_catalogVersion);
      markStateChanged();
    }
  }

  void Store::timeSynchronized() {
    std::lock_guard guard(_mutex);
    const auto now = monotonicMs();
    const auto offset = utcMs() - now;
    if (!_clockTrusted || std::abs(offset - _clockOffset) > 2000)
      _clockStableAt = now;
    _clockOffset = offset;
    _clockTrusted = true;
    trustedNowLocked();
    _nextMaintenance = 0;
    if (_task) xTaskNotifyGive(_task);
  }

  int64_t Store::trustedNowLocked() {
    int64_t result = 0;
    if (_clockTrusted) {
      const auto monotonic = monotonicMs(), utc = utcMs();
      const auto offset = utc - monotonic;
      if (std::abs(offset - _clockOffset) > 2000)
        _clockTrusted = false;  // Require a new explicit synchronization event.
      // Revalidate after clock adjustments before expiring any history.
      else if (monotonic - _clockStableAt >= 60000) result = utc;
    }
    if (_timeValid != (result != 0)) {
      _timeValid = result != 0;
      markStateChanged(true);
    }
    return result;
  }

  bool Store::append(const LogMessage* message) {
    if (!message) return _capture.load();
    if (!_capture.load()) {
      if (_enabled.load()) { ++_dropped; markStateChanged(); }
      return true;
    }
    _producers.fetch_add(1);
    struct ProducerDone {
      std::atomic<uint32_t>& count;
      ~ProducerDone() { count.fetch_sub(1); }
    } done{_producers};
    if (!_capture.load()) { ++_dropped; markStateChanged(); return false; }
    if (!_queue || !xPortCanYield() ||
        xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) {
      ++_dropped;
      markStateChanged();
      return false;
    }
    const auto bytes = sizeof(Queued) +
        std::min(message->size(), ItemBytes - sizeof(Queued));
    void* item = nullptr;
    if (xRingbufferSendAcquire(_queue, &item, bytes, 0) != pdTRUE) {
      ++_dropped;
      markStateChanged();
      return false;
    }
    auto* queued = static_cast<Queued*>(item);
    queued->truncated = false;
    queued->valid = message->copyTo(queued + 1, bytes - sizeof(Queued),
                                   &queued->truncated) != nullptr;
    if (queued->truncated) ++_truncated;
    _pending.fetch_add(bytes);
    xRingbufferSendComplete(_queue, item);
    markStateChanged();
    return true;
  }

  const JsonVariantConst Store::descriptor() const {
    static JsonDocument description = []() {
      JsonDocument result;
      result["schema"] = 1;
      return result;
    }();
    return description.as<JsonVariantConst>();
  }

  JsonDocument* Store::getInfo(RequestContext&) {
    std::lock_guard guard(_mutex);
    auto* doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    const auto reserve = reserveLocked();
    root["schema"] = 1;
    root["available"] = _root.ready;
    root["filesystem"] = _root.backend;
    root["label"] = _root.label;
    root["totalBytes"] = _root.total;
    root["freeBytes"] = _root.total >= _root.used ? _root.total - _root.used : 0;
    root["supportsQuotaPercent"] = true;
    root["quotaBaseBytes"] = quotaBaseLocked();
    root["reserveBytes"] = reserve;
    root["minQuotaBytes"] = storage::Engine::MinimumQuota;
    root["maxQuotaBytes"] = _root.total > reserve ? _root.total - reserve : 0;
    root["pageBytes"] = 4096;
    root["maxPageBytes"] = storage::Engine::MaxRead;
    return doc;
  }

  JsonDocument* Store::getState(RequestContext&) {
    std::lock_guard guard(_mutex);
    const auto status = _engine.status();
    auto* doc = new JsonDocument();
    auto root = doc->to<JsonObject>();
    root["available"] = _root.ready;
    root["enabled"] = _enabled.load();
    root["ready"] = _ready && !_suspended && !_otaPaused &&
                    (!_enabled || (_capture.load() && _task && _queue));
    root["usedBytes"] = status.usedBytes;
    root["quotaBytes"] = quotaBudgetLocked();
    root["quotaPercent"] = _policy.quotaPercent;
    root["maxAgeDays"] = _policy.maxAgeDays;
    root["quotaBaseBytes"] = quotaBaseLocked();
    root["effectiveQuotaBytes"] = status.effectiveQuotaBytes;
    root["freeBytes"] = _root.total >= _root.used ? _root.total - _root.used : 0;
    root["reserveBytes"] = reserveLocked();
    root["fileCount"] = status.fileCount;
    root["dropped"] = _dropped.load();
    root["truncated"] = _truncated.load();
    root["pendingBytes"] = _pending.load();
    root["workerRunning"] = _task != nullptr;
    root["captureQueueBytes"] = _queue ? QueueBytes : 0;
    root["timeValid"] = _timeValid;
    root["exporting"] = _exportActive.load();
    root["catalogGeneration"] = _generation;
    root["lastError"] = _error;
    return doc;
  }

  JsonDocument* Store::getConfig(RequestContext&) {
    std::lock_guard guard(_mutex);
    auto* doc = new JsonDocument();
    (*doc)["enabled"] = _enabled.load();
    (*doc)["quotaBytes"] = quotaBudgetLocked();
    (*doc)["quotaPercent"] = _policy.quotaPercent;
    (*doc)["maxAgeDays"] = _policy.maxAgeDays;
    return doc;
  }

  bool Store::setConfig(RequestContext& context) {
    std::lock_guard guard(_mutex);
    const auto data = context.data;
    auto policy = _policy;
    bool enabled = _enabled;
    if (!data.is<JsonObjectConst>() ||
        (!data["enabled"].isUnbound() && !data["enabled"].is<bool>()) ||
        (!data["quotaBytes"].isUnbound() && !data["quotaBytes"].is<uint32_t>()) ||
        (!data["quotaPercent"].isUnbound() && !data["quotaPercent"].is<uint32_t>()) ||
        (!data["maxAgeDays"].isUnbound() && !data["maxAgeDays"].is<uint32_t>())) {
      context.errors.check(ESP_ERR_INVALID_ARG, "Invalid logging settings");
      return false;
    }
    if (!data["enabled"].isUnbound()) enabled = data["enabled"].as<bool>();
    if (!data["quotaPercent"].isUnbound())
      policy.quotaPercent = data["quotaPercent"].as<uint32_t>();
    else if (!data["quotaBytes"].isUnbound())
      policy.quotaPercent = 0;  // Preserve legacy saved fixed-byte settings.
    if (!policy.quotaPercent && !data["quotaBytes"].isUnbound())
      policy.quotaBytes = data["quotaBytes"];
    if (!data["maxAgeDays"].isUnbound()) policy.maxAgeDays = data["maxAgeDays"];
    if (policy.quotaPercent > 100 ||
        (!policy.quotaPercent && policy.quotaBytes < storage::Engine::MinimumQuota) ||
        policy.maxAgeDays > 3650) {
      context.errors.check(ESP_ERR_INVALID_ARG, "Quota or retention is out of range");
      return false;
    }
    if (!context.request.isInternal()) {
      // Capacity providers may not see stdio-buffered records until flush.
      // Sample only after flushing, so own pending bytes are not mistaken for
      // space reclaimed from other application data.
      if (_ready && !_suspended) {
        const auto flushed = _engine.flush();
        if (!applyLocked(flushed) && enabled) {
          context.errors.check(ESP_FAIL, "Cannot flush logs before applying settings");
          return false;
        }
      }
      const auto capacity = capacityLocked();
      const auto baseline = storage::Engine::quotaBase(capacity, _engine.status().usedBytes);
      const auto budget = storage::Engine::quotaBudget(policy, capacity, _engine.status().usedBytes);
      if ((enabled && (!_root.ready || capacity.total < capacity.reserve ||
                        std::min(budget, baseline > capacity.reserve ?
                            baseline - capacity.reserve : 0) < storage::Engine::MinimumQuota)) ||
          (!policy.quotaPercent && _root.ready &&
           policy.quotaBytes > (_root.total > capacity.reserve ?
               _root.total - capacity.reserve : 0))) {
        context.errors.check(ESP_ERR_INVALID_STATE,
                             "Logging quota exceeds available filesystem capacity");
        return false;
      }
    }
    const bool changed = enabled != _enabled ||
        policy.quotaPercent != _policy.quotaPercent ||
        (!policy.quotaPercent && policy.quotaBytes != _policy.quotaBytes) ||
        policy.maxAgeDays != _policy.maxAgeDays;
    if (!changed) return false;
    _capture = false;
    if (_ready) {
      // Configuration can immediately require reclaiming an export's files.
      // Stop its network task; housekeeping applies the new policy once unpinned.
      cancelExportLocked();
      if (!enabled) {
        _draining = true;
        drainLocked();
        if (!_pending && !_producers) {
          applyLocked(_engine.seal());
          _sessionMarked = false;
          _draining = false;
          releaseQueueLocked();
        }
      }
    }
    _enabled = enabled;
    _policy = policy;
    _nextMaintenance = 0;
    markStateChanged(true);
    if (_task) xTaskNotifyGive(_task);
    return true;
  }

  bool Store::handleRequest(Request& request) {
    if (AppObject::handleRequest(request)) return true;
    if (!request.is("list") && !request.is("read")) return false;
    std::lock_guard guard(_mutex);
    if (!_ready || _suspended || _otaPaused) {
      request.respondError("LOGS_UNAVAILABLE");
      return true;
    }
    catalogLocked();
    const auto data = request.data();
    JsonDocument doc;
    auto root = doc.to<JsonObject>();
    root["generation"] = _generation;
    if (request.is("list")) {
      auto files = root["files"].to<JsonArray>();
      for (const auto& file : _engine.files()) {
        auto entry = files.add<JsonObject>();
        entry["id"] = file.id;
        entry["size"] = file.size;
        entry["active"] = file.active;
        entry["bootId"] = file.bootId;
        if (file.timeKnown) {
          entry["firstTime"] = file.firstTime;
          entry["lastTime"] = file.lastTime;
        } else {
          entry["firstTime"] = nullptr;
          entry["lastTime"] = nullptr;
        }
      }
    } else {
      if (_exportActive) { request.respondError("LOGS_BUSY"); return true; }
      if (!data["id"].is<const char*>() ||
          (!data["offset"].isUnbound() && !data["offset"].is<uint32_t>()) ||
          (!data["limit"].isUnbound() && !data["limit"].is<uint32_t>()) ||
          (!data["tail"].isUnbound() && !data["tail"].is<bool>())) {
        request.respondError("LOGS_INVALID_CURSOR"); return true;
      }
      const std::string id = data["id"].as<const char*>();
      if (!storage::Engine::validId(id)) {
        request.respondError("LOGS_INVALID_ID"); return true;
      }
      if (!data["generation"].isUnbound() &&
          (!data["generation"].is<const char*>() ||
           _generation != data["generation"].as<const char*>())) {
        request.respondError("LOGS_STALE"); return true;
      }
      const size_t limit = data["limit"] | size_t(4096);
      if (limit < 4 || limit > storage::Engine::MaxRead) {
        request.respondError("LOGS_INVALID_LIMIT"); return true;
      }
      size_t offset = data["offset"] | size_t(0);
      if (data["tail"] | false) {
        const auto file = std::find_if(_engine.files().begin(), _engine.files().end(),
            [&id](const auto& item) { return item.id == id; });
        if (file == _engine.files().end()) {
          request.respondError("LOGS_PRUNED"); return true;
        }
        offset = file->size > limit ? file->size - limit : 0;
      }
      storage::Read page;
      const auto result = _engine.read(id, offset, limit, page);
      if (!result) { request.respondError("LOGS_READ_FAILED"); return true; }
      utf8Page(page);
      root["id"] = id;
      root["offset"] = page.offset;
      root["end"] = page.nextOffset;
      root["size"] = page.size;
      root["text"] = page.text;
      root["eof"] = page.eof;
    }
    request.respond(root, false);
    return true;
  }

  void Store::cancelExportLocked() {
    if (!_exportActive) return;
    if (!_exportCancel.exchange(true)) markStateChanged(true);
    if (_exportFd >= 0) shutdown(_exportFd, SHUT_RDWR);
  }

  bool Store::beforeFormat() {
    _capture = false;
    _suspended = true;
    markStateChanged(true);
    {
      std::lock_guard guard(_mutex);
      cancelExportLocked();
    }
    const auto until = monotonicMs() + 5000;
    while (_exportActive && monotonicMs() < until) vTaskDelay(pdMS_TO_TICKS(10));
    std::lock_guard guard(_mutex);
    if (_exportActive) {
      _suspended = false;
      recordErrorLocked("Log export did not stop; filesystem format refused");
      return false;
    }
    drainLocked();
    if (_producers || _pending) {
      _suspended = false;
      recordErrorLocked("Log capture did not stop; filesystem format refused");
      return false;
    }
    releaseQueueLocked();
    applyLocked(_engine.seal());
    _engine.reset();
    _sessionMarked = false;
    _ready = false;
    return true;
  }

  void Store::afterFormat(bool success) {
    std::lock_guard guard(_mutex);
    _reopen = true;
    _suspended = false;
    _nextMaintenance = 0;
    markStateChanged(true);
    if (!success) recordErrorLocked("Filesystem format failed");
    if (_task) xTaskNotifyGive(_task);
  }

  void Store::handleEvent(Event& event) {
    if (EventPeriodic::is(event)) {
      {
        std::lock_guard guard(_mutex);
        // Retained-file age/quota maintenance needs no idle queue or task stack.
        // Also retry transient capture allocation/capacity failures here.
        if (_initialized && !_task) maintainLocked();
      }
      publishStateChanged();
    } else if (net::EventTimeSync::is(event)) timeSynchronized();
    else if (EventInit::is(event, 0)) {
      std::lock_guard guard(_mutex);
      if (!_prepared) _prepared = true;
      const auto capacity = capacityLocked();
      // Restore the persisted policy before recovery can prune retained files.
      if (_root.ready) {
        applyLocked(_engine.configure(_policy, capacity));
        _ready = applyLocked(_engine.recover(capacity));
      } else recordErrorLocked("Root filesystem capacity is unavailable");
      catalogLocked();
      _initialized = true;
      maintainLocked();
      const auto registered = ui::Httpd::instance()->addHttpHandler(*this);
      if (registered != ESP_OK) recordErrorLocked("Log download endpoint could not be registered");
      markStateChanged(true);
    } else if (EventDone::is(event, nullptr)) {
      _capture = false;
      std::lock_guard guard(_mutex);
      drainLocked();
      _suspended = true;
      cancelExportLocked();
      applyLocked(_engine.seal());
      releaseQueueLocked();
      markStateChanged(true);
    }
  }

  void Store::writeQueuedLocked(const Queued* queued) {
    const auto* message = reinterpret_cast<const LogMessage*>(queued + 1);
    auto* formatted = formatter()(message);
    if (!formatted) { ++_dropped; markStateChanged(); return; }
    const auto length = strnlen(formatted, storage::Engine::MaxRecord + 1);
    std::string text(formatted, std::min(length, storage::Engine::MaxRecord - 16));
    free(formatted);
    if (queued->truncated || length > storage::Engine::MaxRecord - 16) {
      text += " [truncated]";
      if (!queued->truncated) ++_truncated;
    }
    const auto current = _engine.accountedCapacity();
    const auto result = _root.ready ? _engine.append(text,
        _clockTrusted && message->stamp() < 0 ? -message->stamp() : 0,
        _bootId, current) : storage::Result{false, false,
            "Root filesystem capacity is unavailable"};
    if (!applyLocked(result)) { ++_dropped; _capture = false; }
    else {
      _bytesSinceFlush += text.size() + 1;
      if (_bytesSinceFlush >= 4096) {
        if (!applyLocked(_engine.flush())) _capture = false;
        _bytesSinceFlush = 0;
      }
    }
    catalogLocked();
    markStateChanged();
  }

  void Store::drainLocked() {
    // Producers do not take _mutex. After capture is disabled they can finish
    // publishing any already acquired slot while this bounded drain runs.
    const auto until = monotonicMs() + 2000;
    while (_queue && monotonicMs() < until) {
      size_t bytes = 0;
      auto* queued = static_cast<Queued*>(xRingbufferReceive(_queue, &bytes, 0));
      if (queued) {
        if (queued->valid && (_enabled || _draining) && _ready && !_suspended &&
            !net::ota::isRunning()) writeQueuedLocked(queued);
        else ++_dropped;
        _pending.fetch_sub(bytes);
        markStateChanged();
        vRingbufferReturnItem(_queue, queued);
      } else if (!_producers) break;
      else vTaskDelay(1);
    }
  }

  bool Store::startWorkerLocked(bool captureQueue) {
    if (captureQueue && !_queue) {
      _queue = xRingbufferCreate(QueueBytes, RINGBUF_TYPE_NOSPLIT);
      if (!_queue || xRingbufferGetMaxItemSize(_queue) < ItemBytes) {
        releaseQueueLocked();
        recordErrorLocked("Log queue could not be allocated");
        return false;
      }
    }
    if (!_task) {
      const auto created = xTaskCreate([](void* self) {
        static_cast<Store*>(self)->run();
      }, "m/log-store", 6144, this, tskIDLE_PRIORITY, &_task);
      if (created != pdPASS) {
        _task = nullptr;
        releaseQueueLocked();
        recordErrorLocked("Log storage worker could not be started");
        return false;
      }
    }
    return true;
  }

  void Store::releaseQueueLocked() {
    // Producers increment their count, then recheck capture before touching
    // _queue. A late uncounted caller can only return, or use the new queue
    // after capture starts again; an acquired slot keeps its queue alive.
    if (_queue && !_capture && !_producers && !_pending) {
      vRingbufferDelete(_queue);
      _queue = nullptr;
    }
  }

  void Store::maintainLocked() {
    const auto previousFlags = stateFlagsLocked();
    // Remains independent of the export task's potentially blocked send.
    if (_exportActive && monotonicMs() >= _exportDeadline) cancelExportLocked();
    const bool ota = net::ota::isRunning();
    if (ota && !_otaPaused) {
      _capture = false;
      cancelExportLocked();
      applyLocked(_engine.seal());
    }
    if (_otaPaused != ota) _nextMaintenance = 0;
    _otaPaused = ota;
    if (_draining) {
      drainLocked();
      if (!_producers && !_pending) {
        applyLocked(_engine.seal());
        _sessionMarked = false;
        _draining = false;
      }
    }
    if (!_draining && !_suspended && !ota && monotonicMs() >= _nextMaintenance) {
      const auto now = trustedNowLocked();
      const bool flushed = !_ready || applyLocked(_engine.flush());
      auto capacity = capacityLocked();
      if (!_root.ready) {
        // Unknown capacity must never be fed into pruning as zero capacity.
        _capture = false;
        recordErrorLocked("Root filesystem capacity is unavailable");
      } else if (_reopen || !_ready) {
        _engine.reset();
        applyLocked(_engine.configure(_policy, capacity));
        _ready = applyLocked(_engine.recover(capacity));
        _reopen = false;
        capacity = capacityLocked();
        catalogLocked();
      }
      if (_ready && _root.ready) {
        const bool ok = flushed &&
            applyLocked(_engine.configure(_policy, capacity, now));
        _bytesSinceFlush = 0;
        const bool budgetReady = _engine.status().effectiveQuotaBytes >=
                                  storage::Engine::MinimumQuota;
        const bool wanted = _enabled && ok && budgetReady;
        if (!budgetReady) recordErrorLocked("Log quota is below the minimum usable storage budget");
        if (wanted && !_sessionMarked) {
          char marker[256];
          snprintf(marker, sizeof(marker), "# application=%s version=%s",
                   App::instance().name(), App::instance().version() ?
                   App::instance().version() : "unknown");
          const auto current = _engine.accountedCapacity();
          _sessionMarked = applyLocked(_engine.append(marker,
              _clockTrusted ? utcMs() : 0, _bootId, current));
        }
        _capture = wanted && _sessionMarked && startWorkerLocked(true);
        if (ok && budgetReady && (!_enabled || _capture.load())) applyLocked({}, true);
        catalogLocked();
      } else _capture = false;
      _nextMaintenance = monotonicMs() + 1000;
    }
    if (!_capture) {
      drainLocked();
      releaseQueueLocked();
    }
    if (stateFlagsLocked() != previousFlags) markStateChanged(true);
  }

  void Store::run() {
    for (;;) {
      if (!_pending.load()) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
      bool retiring = false;
      Queued* queued = nullptr;
      {
        std::lock_guard guard(_mutex);
        maintainLocked();
        // Receive under the same lock as a graceful drain, so no accepted
        // record can be hidden in a worker-local variable during disable.
        size_t itemBytes = 0;
        if (_queue) queued = static_cast<Queued*>(xRingbufferReceive(_queue, &itemBytes, 0));
        if (queued) {
          if (queued->valid && !_suspended && !_otaPaused && _ready && _enabled && _capture)
            writeQueuedLocked(queued);
          else ++_dropped;
          _pending.fetch_sub(itemBytes);
          markStateChanged();
          vRingbufferReturnItem(_queue, queued);
        }
        if (!_capture && !_exportActive && !_pending && !_producers) {
          releaseQueueLocked();
          // Handle reads/notifications outside producers are under _mutex.
          // A concurrently restarted worker cannot be this retiring task.
          _task = nullptr;
          markStateChanged(true);
          retiring = true;
        }
      }
      publishStateChanged();
      if (retiring) { vTaskDelete(nullptr); return; }
      if (!queued && _pending.load()) vTaskDelay(1);
    }
  }

  esp_err_t Store::handle(httpd_req_t* request) {
    const auto querySize = httpd_req_get_url_query_len(request);
    if (querySize > 96) {
      httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid log request");
      return ESP_OK;
    }
    char query[97]{}, id[32]{};
    if (querySize) {
      if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
          httpd_query_key_value(query, "id", id, sizeof(id)) != ESP_OK ||
          !storage::Engine::validId(id)) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid log id");
        return ESP_OK;
      }
    }
    std::lock_guard guard(_mutex);
    if (!_ready || _suspended || net::ota::isRunning() || _exportActive) {
      httpd_resp_set_status(request, "503 Service Unavailable");
      httpd_resp_sendstr(request, "Log storage unavailable or busy");
      return ESP_OK;
    }
    auto* job = new (std::nothrow) Export{this, nullptr, {}};
    if (!job) return ESP_ERR_NO_MEM;
    const auto snapshot = _engine.snapshot(id, job->snapshot);
    if (!snapshot || job->snapshot.files.empty()) {
      if (snapshot) _engine.releaseSnapshot(job->snapshot.token);
      delete job;
      httpd_resp_send_404(request);
      return ESP_OK;
    }
    // The capture queue is unnecessary for retained-file downloads, but the
    // independent watchdog must exist before an async send can begin.
    if (!startWorkerLocked(false)) {
      _engine.releaseSnapshot(job->snapshot.token);
      delete job;
      httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot start export watchdog");
      return ESP_OK;
    }
    const auto async = httpd_req_async_handler_begin(request, &job->request);
    if (async != ESP_OK) {
      _engine.releaseSnapshot(job->snapshot.token);
      delete job;
      httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot start export");
      return ESP_OK;
    }
    _exportFd = httpd_req_to_sockfd(job->request);
    _exportDeadline = monotonicMs() + 120000;
    _exportActive = true;
    _exportCancel = false;
    markStateChanged(true);
    const auto created = xTaskCreate([](void* raw) {
      auto* job = static_cast<Export*>(raw);
      job->owner->exportFile(job);
    }, "m/log-export", 6144, job, 1, nullptr);
    if (created != pdPASS) {
      _engine.releaseSnapshot(job->snapshot.token);
      _exportFd = -1;
      _exportActive = false;
      markStateChanged(true);
      httpd_resp_send_err(job->request, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot start export worker");
      httpd_req_async_handler_complete(job->request);
      delete job;
    }
    return ESP_OK;
  }

  void Store::exportFile(Export* job) {
    auto* request = job->request;
    const auto fd = httpd_req_to_sockfd(request);
    timeval sendTimeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));
    const auto deadline = monotonicMs() + 120000;
    const auto filename = std::string("attachment; filename=\"logs-") +
        (job->snapshot.files.size() == 1 ? job->snapshot.files.front().id : _bootId) + ".txt\"";
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Content-Disposition", filename.c_str());
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    bool ok = true;
    for (size_t index = 0; ok && index < job->snapshot.files.size(); ++index) {
      const auto& file = job->snapshot.files[index];
      if (job->snapshot.files.size() > 1) {
        const auto delimiter = "\n# ===== " + file.id + " =====\n";
        ok = !_exportCancel && httpd_resp_send_chunk(request, delimiter.data(), delimiter.size()) == ESP_OK;
      }
      size_t offset = 0;
      while (ok && offset < file.size) {
        if (_exportCancel || _suspended || net::ota::isRunning() || monotonicMs() >= deadline) {
          ok = false;
          break;
        }
        storage::Read page;
        {
          std::lock_guard guard(_mutex);
          ok = bool(_engine.readSnapshot(job->snapshot.token, index, offset, 4096, page));
        }
        if (!ok || page.text.empty()) { ok = false; break; }
        ok = httpd_resp_send_chunk(request, page.text.data(), page.text.size()) == ESP_OK;
        offset = page.nextOffset;
        vTaskDelay(1);
      }
    }
    if (_exportCancel || monotonicMs() >= deadline) ok = false;
    if (ok) ok = httpd_resp_send_chunk(request, nullptr, 0) == ESP_OK;
    if (!ok) shutdown(fd, SHUT_RDWR);  // No normal end marker for a partial export.
    {
      std::lock_guard guard(_mutex);
      _engine.releaseSnapshot(job->snapshot.token);
      _exportFd = -1;
    }
    httpd_req_async_handler_complete(request);
    delete job;
    {
      std::lock_guard guard(_mutex);
      _exportActive = false;
      _nextMaintenance = 0;
      markStateChanged(true);
      if (_task) xTaskNotifyGive(_task);
    }
    vTaskDelete(nullptr);
  }
}  // namespace esp32m::log
