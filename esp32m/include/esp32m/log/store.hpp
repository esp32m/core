#pragma once

#include "esp32m/app.hpp"
#include "esp32m/fs/root.hpp"
#include "esp32m/log/storage.hpp"
#include "esp32m/ui/httpd.hpp"

#include <freertos/ringbuf.h>
#include <atomic>
#include <mutex>

namespace esp32m::log {
  class Store : public AppObject, public LogAppender, public ui::IHttpHandler {
   public:
    static Store& instance();
    const char* name() const override { return "logs"; }
    // Called after root mount, before Config::load(). Does not start disk output.
    void prepare();
    // Custom clock providers may explicitly signal a trusted UTC synchronization.
    void timeSynchronized();
    esp_err_t handle(httpd_req_t* request) override;

   protected:
    bool append(const LogMessage* message) override;
    void handleEvent(Event& event) override;
    bool handleRequest(Request& request) override;
    const JsonVariantConst descriptor() const override;
    JsonDocument* getInfo(RequestContext&) override;
    JsonDocument* getState(RequestContext&) override;
    JsonDocument* getConfig(RequestContext&) override;
    bool setConfig(RequestContext&) override;

   private:
    Store();
    struct Queued { bool truncated, valid; };
    struct Export;
    static constexpr size_t QueueBytes = 8192, ItemBytes = 2048;
    std::mutex _mutex;
    storage::Engine _engine;
    storage::Policy _policy{256 * 1024, 7, 50};
    fs::RootInfo _root;
    std::string _bootId, _generation, _catalogSignature, _error;
    uint64_t _catalogVersion = 0;
    RingbufHandle_t _queue = nullptr;
    TaskHandle_t _task = nullptr;
    std::atomic<bool> _capture{false}, _suspended{false}, _exportActive{false};
    std::atomic<bool> _exportCancel{false}, _clockTrusted{false};
    std::atomic<uint32_t> _dropped{0}, _truncated{0}, _pending{0};
    std::atomic<uint32_t> _producers{0};
    std::atomic<bool> _enabled{false};
    // Producers only invalidate. The worker emits ordinary AppObject hints,
    // coalescing counters/storage telemetry without retaining another JSON view.
    std::atomic<uint8_t> _stateDirty{2};
    std::atomic_flag _publishingState = ATOMIC_FLAG_INIT;
    int64_t _nextStatePublish = 0;
    bool _prepared = false, _ready = false, _reopen = false;
    bool _sessionMarked = false;
    bool _otaPaused = false, _timeValid = false;
    int _exportFd = -1;
    int64_t _exportDeadline = 0;
    size_t _reserveBytes = 0, _bytesSinceFlush = 0;
    int64_t _clockOffset = 0, _clockStableAt = 0;
    uint64_t reserveId();
    storage::Capacity capacityLocked();
    size_t reserveLocked() const;
    size_t quotaBaseLocked() const;
    size_t quotaBudgetLocked() const;
    int64_t trustedNowLocked();
    bool applyLocked(const storage::Result& result, bool clearError = false);
    void catalogLocked();
    void cancelExportLocked();
    bool beforeFormat();
    void afterFormat(bool success);
    void run();
    void writeQueuedLocked(const Queued* queued);
    void drainLocked();
    void exportFile(Export* job);
    void recordErrorLocked(const std::string& error);
    void markStateChanged(bool urgent = false);
    void publishStateChanged();
    uint32_t stateFlagsLocked() const;
  };
}  // namespace esp32m::log
