#include "esp32m/ui/subscriptions.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace esp32m::ui {
  namespace {
    constexpr uint32_t ReconcileMs = 1000;
    constexpr uint32_t CoalesceMs = 25;

    struct AppendWriter {
      std::string& destination;
      size_t write(uint8_t ch) { destination += char(ch); return 1; }
      size_t write(const uint8_t* data, size_t size) {
        destination.append((const char*)data, size);
        return size;
      }
    };
    void appendJson(std::string& result, JsonVariantConst value) {
      AppendWriter writer{result};
      serializeJson(value, writer);
    }

    void appendString(std::string& result, const std::string& value) {
      static constexpr char Hex[] = "0123456789abcdef";
      result += '"';
      for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') {
          result += '\\';
          result += ch;
        } else if (ch < 32) {
          result += "\\u00";
          result += Hex[ch >> 4];
          result += Hex[ch & 15];
        } else
          result += ch;
      }
      result += '"';
    }

    std::string canonical(JsonVariantConst value) {
      std::string result;
      if (value.is<JsonObjectConst>()) {
        std::map<std::string, JsonVariantConst> fields;
        for (auto field : value.as<JsonObjectConst>())
          fields.emplace(field.key().c_str(), field.value());
        result = "{";
        for (auto& [key, child] : fields) {
          if (result.size() > 1)
            result += ',';
          appendString(result, key);
          result += ':';
          result += canonical(child);
        }
        result += '}';
      } else if (value.is<JsonArrayConst>()) {
        result = "[";
        for (auto child : value.as<JsonArrayConst>()) {
          if (result.size() > 1)
            result += ',';
          result += canonical(child);
        }
        result += ']';
      } else
        serializeJson(value, result);
      return result;
    }

    bool equal(JsonVariantConst a, JsonVariantConst b) {
      if (a.is<JsonObjectConst>() && b.is<JsonObjectConst>()) {
        auto ao = a.as<JsonObjectConst>(), bo = b.as<JsonObjectConst>();
        if (ao.size() != bo.size())
          return false;
        for (auto field : ao) {
          auto child = bo[field.key()];
          if (child.isUnbound() || !equal(field.value(), child))
            return false;
        }
        return true;
      }
      if (a.is<JsonArrayConst>() && b.is<JsonArrayConst>()) {
        if (a.size() != b.size())
          return false;
        for (size_t i = 0; i < a.size(); i++)
          if (!equal(a[i], b[i]))
            return false;
        return true;
      }
      // ArduinoJson arithmetic comparison also equates bool and numeric 0/1.
      // JSON scalar categories must remain distinct for state synchronization.
      if (a.is<JsonObjectConst>() || b.is<JsonObjectConst>() ||
          a.is<JsonArrayConst>() || b.is<JsonArrayConst>())
        return false;
      if (a.isNull() != b.isNull() || a.is<bool>() != b.is<bool>() ||
          a.is<const char*>() != b.is<const char*>())
        return false;
      return a == b;
    }

    void operation(JsonArray patch, JsonArrayConst path, const char* op,
                   JsonVariantConst value = JsonVariantConst()) {
      auto item = patch.add<JsonObject>();
      item["op"] = op;
      item["path"] = path;
      if (!strcmp(op, "set"))
        item["value"] = value;
    }

    void diff(JsonVariantConst before, JsonVariantConst after, JsonArray path,
              JsonArray patch, unsigned depth = 0) {
      if (equal(before, after))
        return;
      if (depth < 24 && before.is<JsonObjectConst>() &&
          after.is<JsonObjectConst>()) {
        auto a = before.as<JsonObjectConst>(), b = after.as<JsonObjectConst>();
        for (auto field : a)
          if (b[field.key()].isUnbound()) {
            path.add(field.key().c_str());
            operation(patch, path, "remove");
            path.remove(path.size() - 1);
          }
        for (auto field : b) {
          path.add(field.key().c_str());
          auto previous = a[field.key()];
          if (previous.isUnbound())
            operation(patch, path, "set", field.value());
          else
            diff(previous, field.value(), path, patch, depth + 1);
          path.remove(path.size() - 1);
        }
      } else if (depth < 24 && before.is<JsonArrayConst>() &&
                 after.is<JsonArrayConst>() && before.size() == after.size()) {
        for (size_t i = 0; i < before.size(); i++) {
          path.add(i);
          diff(before[i], after[i], path, patch, depth + 1);
          path.remove(path.size() - 1);
        }
      } else
        operation(patch, path, "set", after);
    }

  }  // namespace

  // Count real ArduinoJson allocations, including pool/slack and allocation
  // headers, rather than its deprecated memoryUsage() or JSON wire length.
  class StateSubscriptions::Allocator : public ArduinoJson::Allocator {
   public:
    Allocator(size_t& total, size_t limit) : total(total), limit(limit) {}
    void* allocate(size_t size) override {
      if (size + sizeof(Header) > limit - std::min(limit, used) ||
          size + sizeof(Header) > MaxBaselineBytes + MaxWorkingBytes -
                                     std::min(MaxBaselineBytes + MaxWorkingBytes,
                                              total))
        return nullptr;
      auto* header = static_cast<Header*>(malloc(sizeof(Header) + size));
      if (!header)
        return nullptr;
      header->size = sizeof(Header) + size;
      used += header->size;
      total += header->size;
      return header + 1;
    }
    void deallocate(void* pointer) override {
      if (!pointer)
        return;
      auto* header = static_cast<Header*>(pointer) - 1;
      used -= header->size;
      total -= header->size;
      free(header);
    }
    void* reallocate(void* pointer, size_t size) override {
      if (!pointer)
        return allocate(size);
      if (!size) {
        deallocate(pointer);
        return nullptr;
      }
      auto* old = static_cast<Header*>(pointer) - 1;
      if (size <= old->size - sizeof(Header))
        return pointer;
      auto* replacement = allocate(size);
      if (replacement) {
        memcpy(replacement, pointer, old->size - sizeof(Header));
        deallocate(pointer);
      }
      return replacement;
    }
    size_t used = 0;

   private:
    struct alignas(std::max_align_t) Header { size_t size; };
    size_t& total;
    size_t limit;
  };

  struct StateSubscriptions::Snapshot {
    Allocator allocator;
    JsonDocument doc;
    explicit Snapshot(size_t& total, size_t limit = MaxSnapshotBytes)
        : allocator(total, limit), doc(&allocator) {}
  };
  struct StateSubscriptions::Stream {
    std::string target, view, query, key, id;
    std::unique_ptr<Snapshot> state;
    uint64_t revision = 0, seenGeneration = 0;
    uint32_t capturedAt = 0;
  };
  struct StateSubscriptions::Interest {
    Session session;
    std::string id;
    Stream* stream;
  };

  StateSubscriptions::StateSubscriptions(Capture capture, Send send, Alive alive,
                                         Close close, Clock clock)
      : _capture(std::move(capture)), _send(std::move(send)),
        _alive(std::move(alive)), _close(std::move(close)),
        _clock(std::move(clock)) {}
  StateSubscriptions::~StateSubscriptions() {
    _streams.clear();  // allocator accounting outlives every document
  }

  size_t StateSubscriptions::baselineBytes() const {
    size_t result = 0;
    for (auto& stream : _streams)
      if (stream->state)
        result += stream->state->allocator.used;
    return result;
  }

  bool StateSubscriptions::send(const Session& session, Payload payload) {
    if (!_alive(session))
      return false;
    if (_send(session, std::move(payload)))
      return true;
    ++_sendFailures;
    return false;
  }

  void StateSubscriptions::reply(const Session& session, const char* name,
                                 int seq, uint32_t now, const char* error,
                                 const Interest* interest) {
    std::string result = "{\"type\":\"response\",\"source\":\"ui\",\"name\":";
    appendString(result, name);
    result += ",\"seq\":" + std::to_string(seq);
    if (error) {
      ++_rejected;
      result += ",\"error\":[";
      appendString(result, error);
      result += ']';
    } else if (interest) {
      auto& stream = *interest->stream;
      result.reserve(result.size() + interest->id.size() + stream.target.size() +
                     stream.id.size() + measureJson(stream.state->doc) + 128);
      result += ",\"data\":{\"id\":";
      appendString(result, interest->id);
      result += ",\"source\":";
      appendString(result, stream.target);
      result += ",\"stream\":";
      appendString(result, stream.id);
      result += ",\"rev\":" + std::to_string(stream.revision) +
                ",\"serverTimeMs\":" + std::to_string(_clock ? _clock() : now) +
                ",\"state\":";
      appendJson(result, stream.state->doc);
      result += '}';
    } else
      result += ",\"data\":null";
    result += '}';
    ++_serialized;
    send(session, std::make_shared<const std::string>(std::move(result)));
  }

  // Keep publication temporaries out of the source serializer's call stack.
  std::unique_ptr<StateSubscriptions::Snapshot> StateSubscriptions::captureNext(
      const Stream& stream, const char*& error) {
    JsonDocument query;
    deserializeJson(query, stream.query);
    auto next = std::make_unique<Snapshot>(_allocated);
    ++_captures;
    error = _capture(stream.target, stream.view, query, next->doc);
    if (!error && next->doc.overflowed())
      error = "snapshot-limit";
    if (error)
      return {};
    return next;
  }

  StateSubscriptions::Payload StateSubscriptions::makeChangePayload(
      const Stream& stream, const Snapshot& next, uint64_t base, uint32_t now) {
    std::string prefix = "{\"type\":\"event\",\"name\":\"state-changed\",\"source\":";
    appendString(prefix, stream.target);
    prefix += ",\"data\":{\"stream\":";
    appendString(prefix, stream.id);
    prefix += ",\"rev\":" + std::to_string(stream.revision) +
              ",\"serverTimeMs\":" + std::to_string(_clock ? _clock() : now);
    Snapshot patch(_allocated, MaxWorkingBytes - MaxSnapshotBytes);
    auto array = patch.doc["patch"].to<JsonArray>();
    auto path = patch.doc["path"].to<JsonArray>();
    diff(stream.state->doc, next.doc, path, array);
    // Select using measured wire lengths, then serialize only the winner.
    // This avoids holding both full-state and delta strings during capture.
    auto deltaHeader = ",\"baseRev\":" + std::to_string(base) + ",\"patch\":";
    size_t stateSize = measureJson(next.doc) + strlen(",\"state\":");
    size_t patchSize = measureJson(array) + deltaHeader.size();
    std::string message = prefix;
    if (!patch.doc.overflowed() && patchSize < stateSize) {
      message.reserve(prefix.size() + patchSize + 2);
      message += deltaHeader;
      appendJson(message, array);
    } else {
      message.reserve(prefix.size() + stateSize + 2);
      message += ",\"state\":";
      appendJson(message, next.doc);
    }
    message += "}}";
    auto payload = std::make_shared<const std::string>(std::move(message));
    ++_serialized;
    return payload;
  }

  bool StateSubscriptions::refresh(Stream& stream, uint32_t now,
                                   const char*& error) {
    uint64_t generation;
    {
      std::lock_guard<std::mutex> lock(_hintMutex);
      generation = _hints[stream.target].generation;
    }
    auto next = captureNext(stream, error);
    if (!next)
      return false;
    auto previousBytes = stream.state ? stream.state->allocator.used : 0;
    if (baselineBytes() - previousBytes + next->allocator.used > MaxBaselineBytes) {
      error = "snapshot-limit";
      return false;
    }
    stream.capturedAt = now;
    stream.seenGeneration = generation;
    {
      std::lock_guard<std::mutex> lock(_hintMutex);
      auto& hint = _hints[stream.target];
      if (hint.generation == generation &&
          std::all_of(_streams.begin(), _streams.end(), [&](auto& item) {
            return item->target != stream.target ||
                   item->seenGeneration == generation;
          }))
        hint.pending = false;
    }
    if (stream.state && equal(stream.state->doc, next->doc))
      return true;
    auto base = stream.revision++;
    Payload payload;
    if (stream.state)
      payload = makeChangePayload(stream, *next, base, now);
    stream.state = std::move(next);
    if (payload) {
      ++_publishes;
      for (size_t i = 0; i < _interests.size(); i++) {
        auto& interest = _interests[i];
        if (interest.stream != &stream)
          continue;
        bool sent = false;
        for (size_t previous = 0; previous < i; previous++)
          if (_interests[previous].stream == &stream &&
              _interests[previous].session == interest.session) {
            sent = true;
            break;
          }
        if (!sent)
          send(interest.session, payload);
      }
    }
    return true;
  }

  void StateSubscriptions::cleanup() {
    _interests.erase(std::remove_if(_interests.begin(), _interests.end(),
                                   [&](auto& item) { return !_alive(item.session); }),
                     _interests.end());
    _streams.erase(std::remove_if(_streams.begin(), _streams.end(), [&](auto& stream) {
      return std::none_of(_interests.begin(), _interests.end(),
                          [&](auto& item) { return item.stream == stream.get(); });
    }), _streams.end());
    std::lock_guard<std::mutex> lock(_hintMutex);
    for (auto i = _hints.begin(); i != _hints.end();)
      if (std::none_of(_streams.begin(), _streams.end(),
                       [&](auto& item) { return item->target == i->first; }))
        i = _hints.erase(i);
      else
        ++i;
  }

  bool StateSubscriptions::request(const Session& session, const char* name,
                                   int seq, JsonVariantConst data, uint32_t now) {
    if (!name)
      return false;
    bool subscribe = !strcmp(name, "state-subscribe"),
         unsubscribe = !strcmp(name, "state-unsubscribe"),
         resync = !strcmp(name, "state-resync");
    if (!subscribe && !unsubscribe && !resync)
      return false;
    cleanup();
    if (!_alive(session))
      return true;
    const char* id = data["id"];
    if (!id || !*id || strlen(id) > 64) {
      reply(session, name, seq, now, "invalid-subscription");
      return true;
    }
    auto existing = std::find_if(_interests.begin(), _interests.end(), [&](auto& item) {
      return item.session == session && item.id == id;
    });
    if (unsubscribe) {
      if (existing != _interests.end())
        _interests.erase(existing);
      cleanup();
      reply(session, name, seq, now, nullptr);
      return true;
    }
    if (resync) {
      const char* error = nullptr;
      if (existing == _interests.end())
        error = "unknown-subscription";
      else
        refresh(*existing->stream, now, error);
      reply(session, name, seq, now, error,
            existing == _interests.end() ? nullptr : &*existing);
      return true;
    }
    const char* target = data["target"], *view = data["view"];
    if (!target || !*target || strlen(target) > 64 || !view ||
        (strcmp(view, "state") && strcmp(view, "device")) ||
        measureJson(data["query"]) > 1024) {
      reply(session, name, seq, now, "invalid-subscription");
      return true;
    }
    std::string query = canonical(data["query"]);
    std::string key = std::string(target) + '\n' + view + '\n' + query;
    if (existing != _interests.end() && existing->stream->key != key) {
      reply(session, name, seq, now, "subscription-conflict");
      return true;
    }
    if (existing != _interests.end()) {
      const char* error = nullptr;
      refresh(*existing->stream, now, error);
      reply(session, name, seq, now, error, &*existing);
      return true;
    }
    if (_interests.size() >= 128 ||
        std::count_if(_interests.begin(), _interests.end(), [&](auto& item) {
          return item.session == session;
        }) >= static_cast<ptrdiff_t>(MaxInterests)) {
      reply(session, name, seq, now, "subscription-limit");
      return true;
    }
    Stream* stream = nullptr;
    for (auto& item : _streams)
      if (item->key == key)
        stream = item.get();
    bool added = false;
    if (!stream) {
      if (_streams.size() >= MaxStreams) {
        reply(session, name, seq, now, "subscription-limit");
        return true;
      }
      auto item = std::make_unique<Stream>();
      item->target = target;
      item->view = view;
      item->query = std::move(query);
      item->key = std::move(key);
      item->id = "s" + std::to_string(++_nextStream);
      stream = item.get();
      _streams.push_back(std::move(item));
      std::lock_guard<std::mutex> lock(_hintMutex);
      _hints.try_emplace(target);
      added = true;
    }
    // Capture/commit updates existing subscribers first. The new interest is
    // activated only after its snapshot is queued on this ordered UI task.
    const char* error = nullptr;
    if (!refresh(*stream, now, error)) {
      reply(session, name, seq, now, error);
      if (added)
        cleanup();
      return true;
    }
    Interest interest{session, id, stream};
    reply(session, name, seq, now, nullptr, &interest);
    if (_alive(session))
      _interests.push_back(std::move(interest));
    return true;
  }

  bool StateSubscriptions::invalidate(const char* source, uint32_t now) {
    if (!source)
      return false;
    std::lock_guard<std::mutex> lock(_hintMutex);
    auto i = _hints.find(source);
    if (i == _hints.end())
      return false;
    if (!i->second.pending)
      i->second.firstAt = now;
    i->second.pending = true;
    ++i->second.generation;
    return true;
  }

  void StateSubscriptions::process(uint32_t now) {
    cleanup();
    for (auto& stream : _streams) {
      bool dirty;
      {
        std::lock_guard<std::mutex> lock(_hintMutex);
        auto& hint = _hints[stream->target];
        dirty = hint.generation != stream->seenGeneration &&
                uint32_t(now - hint.firstAt) >= CoalesceMs;
      }
      if (dirty || uint32_t(now - stream->capturedAt) >= ReconcileMs) {
        const char* error = nullptr;
        if (!refresh(*stream, now, error)) {
          ++_rejected;
          // Losing a stream silently would leave a healthy-looking frozen UI.
          // Reconnect starts from a fresh snapshot and surfaces capture errors.
          if (_close)
            for (auto& interest : _interests)
              if (interest.stream == stream.get())
                _close(interest.session);
          // Retain last valid state; bound retries even on a failing adapter.
          stream->capturedAt = now;
          std::lock_guard<std::mutex> lock(_hintMutex);
          stream->seenGeneration = _hints[stream->target].generation;
        }
      }
    }
  }

  uint32_t StateSubscriptions::waitTime(uint32_t now) const {
    uint32_t wait = ReconcileMs;
    std::lock_guard<std::mutex> lock(_hintMutex);
    for (auto& stream : _streams) {
      auto elapsed = uint32_t(now - stream->capturedAt);
      wait = std::min(wait, elapsed >= ReconcileMs ? 0 : ReconcileMs - elapsed);
      auto i = _hints.find(stream->target);
      if (i != _hints.end() && i->second.generation != stream->seenGeneration) {
        elapsed = uint32_t(now - i->second.firstAt);
        wait = std::min(wait, elapsed >= CoalesceMs ? 0 : CoalesceMs - elapsed);
      }
    }
    return wait;
  }

  void StateSubscriptions::stats(JsonObject result) const {
    result["streams"] = _streams.size();
    result["interests"] = _interests.size();
    result["baselineBytes"] = baselineBytes();
    result["allocatedBytes"] = _allocated;
    result["baselineLimit"] = MaxBaselineBytes;
    result["snapshotLimit"] = MaxSnapshotBytes;
    result["captures"] = _captures;
    result["publishes"] = _publishes;
    result["serialized"] = _serialized;
    result["rejected"] = _rejected;
    result["sendFailures"] = _sendFailures;
  }
}  // namespace esp32m::ui
