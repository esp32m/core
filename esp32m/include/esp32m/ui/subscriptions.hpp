#pragma once

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace esp32m::ui {
  class Transport;

  /** One state baseline per logical stream. All methods except invalidate()
   * run exclusively on the UI task. Payloads contain no per-client state. */
  class StateSubscriptions {
   public:
    struct Session {
      Transport* transport;
      uint32_t cid;
      uint64_t generation;
      bool operator==(const Session& other) const {
        return transport == other.transport && cid == other.cid &&
               generation == other.generation;
      }
    };
    using Payload = std::shared_ptr<const std::string>;
    using Capture = std::function<const char*(const std::string&,
                                             const std::string&,
                                             JsonVariantConst, JsonDocument&)>;
    using Send = std::function<bool(const Session&, Payload)>;
    using Alive = std::function<bool(const Session&)>;
    using Close = std::function<void(const Session&)>;
    using Clock = std::function<uint64_t()>;
    StateSubscriptions(Capture capture, Send send, Alive alive, Close close = {},
                       Clock clock = {});
    ~StateSubscriptions();
    bool request(const Session& session, const char* name, int seq,
                 JsonVariantConst data, uint32_t now);
    bool invalidate(const char* source, uint32_t now);
    void process(uint32_t now);
    uint32_t waitTime(uint32_t now) const;
    void stats(JsonObject result) const;

    static constexpr size_t MaxStreams = 24;
    static constexpr size_t MaxInterests = 32;  // per session
    static constexpr size_t MaxBaselineBytes = 48 * 1024;
    static constexpr size_t MaxSnapshotBytes = 16 * 1024;
    static constexpr size_t MaxWorkingBytes = 24 * 1024;

   private:
    class Allocator;
    struct Snapshot;
    struct Stream;
    struct Interest;
    struct Hint {
      uint64_t generation = 0;
      uint32_t firstAt = 0;
      bool pending = false;
    };
    Capture _capture;
    Send _send;
    Alive _alive;
    Close _close;
    Clock _clock;
    std::vector<std::unique_ptr<Stream>> _streams;
    std::vector<Interest> _interests;
    mutable std::mutex _hintMutex;
    std::map<std::string, Hint> _hints;
    uint64_t _nextStream = 0;
    size_t _allocated = 0;
    uint64_t _captures = 0, _publishes = 0, _serialized = 0,
             _rejected = 0, _sendFailures = 0;
    std::unique_ptr<Snapshot> captureNext(const Stream& stream,
                                          const char*& error);
    Payload makeChangePayload(const Stream& stream, const Snapshot& next,
                              uint64_t base, uint32_t now);
    bool refresh(Stream& stream, uint32_t now, const char*& error);
    void reply(const Session& session, const char* name, int seq, uint32_t now,
               const char* error, const Interest* interest = nullptr);
    void cleanup();
    size_t baselineBytes() const;
    bool send(const Session& session, Payload payload);
  };
}  // namespace esp32m::ui
