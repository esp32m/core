#include "esp32m/log/storage.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <utility>

#ifndef O_BINARY
#define O_BINARY 0
#endif

namespace esp32m {
  namespace log {
    namespace storage {
      namespace {
        constexpr size_t FooterReserve = 192;
        constexpr int64_t DayMs = 86400000;
        std::string ioError(const char* operation) {
          return std::string(operation) + ": " + strerror(errno ? errno : EIO);
        }
        bool validBoot(const std::string& boot) {
          if (boot.empty() || boot.size() > 32) return false;
          for (char c : boot)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
          return true;
        }
        std::string header(const std::string& id, const std::string& boot) {
          return "# esp32m-log v1 id=" + id + " boot=" + boot + "\n";
        }
        bool checkedAdd(size_t a, size_t b, size_t& result) {
          if (b > std::numeric_limits<size_t>::max() - a) return false;
          result = a + b;
          return true;
        }
      }  // namespace

      Engine::Engine(std::string directory, IdSupplier reserveId)
          : _directory(std::move(directory)), _reserveId(std::move(reserveId)) {
        while (_directory.size() > 1 &&
               (_directory.back() == '/' || _directory.back() == '\\'))
          _directory.pop_back();
      }
      Engine::~Engine() { reset(); }
      void Engine::reset() {
        if (_writer) fclose(_writer);
        _writer = nullptr;
        _files.clear();
        _snapshots.clear();
        _activeRecords = 0;
        _activeAllTimesKnown = true;
      }
      bool Engine::validId(const std::string& id) {
        if (id.size() != 16 || id == "0000000000000000") return false;
        for (char c : id)
          if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
      }
      std::string Engine::path(const std::string& id) const {
        return _directory + (_directory == "/" ? "" : "/") + "ml-" + id + ".txt";
      }
      size_t Engine::used() const {
        size_t bytes = 0;
        for (const auto& file : _files) {
          if (!checkedAdd(bytes, file.size, bytes)) return SIZE_MAX;
        }
        return bytes;
      }
      size_t Engine::target() const {
        const size_t countTarget = _effectiveQuota / MaxSegments +
            (_effectiveQuota % MaxSegments ? 1 : 0);
        // Keep small-device segments short, while allowing the bounded catalog
        // to use multi-MiB quotas. Reads/export remain independently chunked.
        return std::max(std::min<size_t>(32768, _effectiveQuota / 4), countTarget);
      }
      void Engine::capacity(const Capacity& value) {
        _capacity = value;
        const size_t owned = used();
        _quotaBase = quotaBase(value, owned);
        _quotaBudget = quotaBudget(_policy, value, owned);
        const size_t available = _quotaBase > value.reserve ?
            _quotaBase - value.reserve : 0;
        _effectiveQuota = std::min(_quotaBudget, available);
      }
      size_t Engine::quotaBase(const Capacity& available, size_t ownedLogBytes) {
        const size_t external = available.used > ownedLogBytes ?
            available.used - ownedLogBytes : 0;
        return available.total - std::min(available.total, external);
      }
      size_t Engine::quotaBudget(const Policy& policy, const Capacity& available,
                                 size_t ownedLogBytes) {
        if (!policy.quotaPercent) return policy.quotaBytes;
        if (policy.quotaPercent > 100) return 0;
        const auto base = quotaBase(available, ownedLogBytes);
        // Compute floor(base * percent / 100) without overflowing size_t.
        return (base / 100) * policy.quotaPercent +
            ((base % 100) * policy.quotaPercent) / 100;
      }
      Segment* Engine::active() {
        for (auto& file : _files) if (file.active) return &file;
        return nullptr;
      }
      Segment* Engine::find(const std::string& id) {
        if (!validId(id)) return nullptr;
        for (auto& file : _files) if (file.id == id) return &file;
        return nullptr;
      }
      bool Engine::pinned(const std::string& id) const {
        for (const auto& snapshot : _snapshots)
          for (const auto& file : snapshot.second.files)
            if (file.id == id) return true;
        return false;
      }
      Result Engine::fail(const std::string& error, bool pressure) {
        _lastError = error;
        return {false, pressure, error};
      }
      Result Engine::writerFailure(const std::string& error) {
        if (_writer) fclose(_writer);
        _writer = nullptr;
        auto file = active();
        if (file) {
          const size_t before = file->size;
          file->active = false;
          file->timeKnown = false;
          struct stat info;
          if (stat(path(file->id).c_str(), &info) == 0 && info.st_size >= 0)
            file->size = static_cast<size_t>(info.st_size);
          if (file->size >= before) _capacity.used += file->size - before;
          else _capacity.used -= std::min(_capacity.used, before - file->size);
        }
        return fail(error);
      }
      Status Engine::status() const {
        return {used(), _effectiveQuota, _files.size(), _lastError,
                _quotaBudget, _quotaBase};
      }
      Result Engine::configure(const Policy& policy, const Capacity& available,
                               int64_t nowMs) {
        if (policy.quotaPercent > 100 ||
            (!policy.quotaPercent && policy.quotaBytes < MinimumQuota) ||
            policy.maxAgeDays > 36500)
          return fail("invalid storage policy");
        _policy = policy;
        return maintenance(nowMs, available);
      }

      Result Engine::prune(size_t extraBytes, size_t extraFiles, int64_t nowMs) {
        const auto cutoff = _policy.maxAgeDays && nowMs > 0
            ? nowMs - static_cast<int64_t>(_policy.maxAgeDays) * DayMs : 0;
        for (;;) {
          size_t required = 0;
          bool full = !checkedAdd(used(), extraBytes, required) ||
              required > _effectiveQuota ||
              _files.size() + extraFiles > MaxSegments;
          auto victim = _files.end();
          for (auto it = _files.begin(); it != _files.end(); ++it) {
            const bool expired = cutoff > 0 && it->timeKnown &&
                it->lastTime > 0 && it->lastTime < cutoff;
            if (it->active || (!full && !expired)) continue;
            // A download must not preserve its old file by sacrificing newer
            // history. The manager cancels it and retries oldest-first pruning.
            if (pinned(it->id))
              return fail(full ? "snapshot blocks storage quota" :
                          "snapshot blocks retention", true);
            victim = it;
            break;
          }
          if (victim == _files.end()) {
            if (full) return fail("storage quota or reserve exhausted");
            return {};
          }
          if (unlink(path(victim->id).c_str()) != 0)
            return fail(ioError("unlink log"));
          _capacity.used = _capacity.used > victim->size ?
              _capacity.used - victim->size : 0;
          _files.erase(victim);
          capacity(_capacity);
        }
      }

      Result Engine::maintenance(int64_t nowMs, const Capacity& available) {
        capacity(available);
        auto file = active();
        const int64_t cutoff = _policy.maxAgeDays && nowMs > 0
            ? nowMs - static_cast<int64_t>(_policy.maxAgeDays) * DayMs : 0;
        if (file && ((used() > _effectiveQuota) ||
                     (cutoff > 0 && file->timeKnown && file->lastTime < cutoff))) {
          auto result = seal();
          if (!result) return result;
        }
        auto result = prune(0, 0, nowMs);
        if (result) _lastError.clear();
        return result;
      }

      Result Engine::create(const std::string& bootId) {
        if (!validBoot(bootId) || !_reserveId) return fail("invalid boot or ID supplier");
        const uint64_t value = _reserveId();
        if (!value) return fail("could not reserve durable log ID");
        char id[17];
        snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(value));
        if (!_files.empty() && std::string(id) <= _files.back().id)
          return fail("log ID supplier is not monotonic");
        auto text = header(id, bootId);
        auto result = prune(text.size() + FooterReserve, 1, 0);
        if (!result) return result;
        int fd = open(path(id).c_str(), O_CREAT | O_EXCL | O_WRONLY | O_BINARY, 0600);
        if (fd < 0) return fail(ioError("create log"));
        _writer = fdopen(fd, "wb");
        if (!_writer) { close(fd); return fail(ioError("open log stream")); }
        if (fwrite(text.data(), 1, text.size(), _writer) != text.size() ||
            fflush(_writer) != 0) {
          const auto error = ioError("write log header");
          fclose(_writer);
          _writer = nullptr;
          // This path was exclusively created by us. Remove failed headers so
          // retrying storage errors cannot accumulate unaccounted orphan files.
          if (unlink(path(id).c_str()) != 0)
            return fail(error + "; " + ioError("remove failed log header"));
          return fail(error);
        }
        Segment file;
        file.id = id;
        file.bootId = bootId;
        file.size = text.size();
        file.active = true;
        _files.push_back(file);
        _capacity.used += text.size();
        _activeRecords = 0;
        _activeAllTimesKnown = true;
        return {};
      }

      Result Engine::append(const std::string& text, int64_t utcMs,
                            const std::string& bootId, const Capacity& available) {
        if (text.empty()) return {};
        if (text.size() > MaxRecord || utcMs < 0 || !validBoot(bootId))
          return fail("invalid or oversized log record");
        capacity(available);
        if (_effectiveQuota < MinimumQuota)
          return fail("effective log quota is below minimum");
        const size_t bytes = text.size() + (text.back() == '\n' ? 0 : 1);
        auto file = active();
        if (file && (file->bootId != bootId ||
                     (file->size + bytes + FooterReserve > target()))) {
          auto result = seal();
          if (!result) return result;
          file = nullptr;
        }
        auto result = prune(bytes + FooterReserve, file ? 0 : 1, 0);
        if (!result) {
          // A reduced quota can require reclaiming the active segment itself.
          if (file && !result.pressure) {
            result = seal();
            if (!result) return result;
            file = nullptr;
            result = prune(bytes + FooterReserve, 1, 0);
          }
          if (!result) return result;
        }
        if (!file) {
          result = create(bootId);
          if (!result) return result;
          file = active();
          result = prune(bytes + FooterReserve, 0, 0);
          if (!result) return result;
        }
        if (!_writer || !file) return fail("log writer unavailable");
        const auto wrote = fwrite(text.data(), 1, text.size(), _writer);
        const bool newline = text.back() == '\n' || fputc('\n', _writer) != EOF;
        if (wrote != text.size() || !newline || ferror(_writer)) {
          return writerFailure(ioError("write log record"));
        }
        file->size += bytes;
        _capacity.used += bytes;
        ++_activeRecords;
        _activeAllTimesKnown = _activeAllTimesKnown && utcMs > 0;
        if (utcMs > 0) {
          if (!file->firstTime || utcMs < file->firstTime) file->firstTime = utcMs;
          file->lastTime = std::max(file->lastTime, utcMs);
        }
        file->timeKnown = _activeAllTimesKnown && _activeRecords > 0;
        _lastError.clear();
        return {};
      }

      Result Engine::flush() {
        if (_writer && fflush(_writer) != 0)
          return writerFailure(ioError("flush log"));
        return {};
      }
      Result Engine::seal() {
        auto file = active();
        if (!file || !_writer) return {};
        char footer[FooterReserve];
        const int length = snprintf(footer, sizeof(footer),
            "# esp32m-log-end v1 id=%s bytes=%llu records=%llu known=%d first=%lld last=%lld\n",
            file->id.c_str(), static_cast<unsigned long long>(file->size),
            static_cast<unsigned long long>(_activeRecords), file->timeKnown ? 1 : 0,
            static_cast<long long>(file->firstTime), static_cast<long long>(file->lastTime));
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(footer))
          return fail("log footer overflow");
        // Footer space was reserved before accepting each record.
        bool ok = fwrite(footer, 1, length, _writer) == static_cast<size_t>(length);
        ok = fflush(_writer) == 0 && ok;
        const auto error = ok ? std::string() : ioError("seal log");
        ok = fclose(_writer) == 0 && ok;
        _writer = nullptr;
        file->active = false;
        struct stat info;
        if (stat(path(file->id).c_str(), &info) != 0 || info.st_size < 0)
          return fail(ioError("stat sealed log"));
        const size_t actual = static_cast<size_t>(info.st_size);
        if (actual >= file->size) _capacity.used += actual - file->size;
        file->size = actual;
        if (!ok) { file->timeKnown = false; return fail(error.empty() ? ioError("close log") : error); }
        return {};
      }

      Result Engine::recover(const Capacity& available, int64_t nowMs) {
        reset();
        _lastError.clear();
        DIR* directory = opendir(_directory.c_str());
        if (!directory) return fail(ioError("open log directory"));
        Result result;
        size_t reclaimed = 0;
        for (;;) {
          errno = 0;
          auto entry = readdir(directory);
          if (!entry) {
            if (errno) result = fail(ioError("read log directory"));
            break;
          }
          std::string name(entry->d_name);
          if (name.size() != 23 || name.compare(0, 3, "ml-") ||
              name.compare(19, 4, ".txt")) continue;
          const auto id = name.substr(3, 16);
          if (!validId(id)) continue;
          struct stat info;
          if (stat(path(id).c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0)
            continue;
          FILE* reader = fopen(path(id).c_str(), "rb");
          if (!reader) { result = fail(ioError("open recovered log")); break; }
          char first[128];
          const bool firstRead = fgets(first, sizeof(first), reader) != nullptr;
          char parsedId[17] = {}, boot[33] = {};
          int consumed = 0;
          const bool owned = firstRead &&
              sscanf(first, "# esp32m-log v1 id=%16[0-9a-f] boot=%32[A-Za-z0-9_-]\n%n",
                     parsedId, boot, &consumed) == 2 &&
              consumed == static_cast<int>(strlen(first)) &&
              id == parsedId && validBoot(boot) && first == header(id, boot);
          if (!owned) { fclose(reader); continue; }
          Segment segment;
          segment.id = id;
          segment.bootId = boot;
          segment.size = static_cast<size_t>(info.st_size);
          // Recovery reads only a bounded tail. A valid complete footer makes
          // timestamps trustworthy; interrupted/unsealed prior boots stay unknown.
          const size_t tailSize = std::min<size_t>(segment.size, MaxRecord + FooterReserve + 1);
          std::string tail(tailSize, '\0');
          if (fseek(reader, static_cast<long>(segment.size - tailSize), SEEK_SET) != 0 ||
              fread(&tail[0], 1, tailSize, reader) != tailSize || ferror(reader)) {
            const auto error = ioError("read recovered log");
            fclose(reader); result = fail(error); break;
          }
          if (fclose(reader) != 0) { result = fail(ioError("close recovered log")); break; }
          size_t complete = tail.find_last_of('\n');
          if (complete == std::string::npos) { result = fail("log has no complete header"); break; }
          const size_t completeSize = segment.size - tailSize + complete + 1;
          if (completeSize != segment.size) {
            if (truncate(path(id).c_str(), static_cast<off_t>(completeSize)) != 0) {
              result = fail(ioError("truncate incomplete log tail")); break;
            }
            reclaimed += segment.size - completeSize;
            segment.size = completeSize;
            tail.resize(complete + 1);
          }
          const auto previous = tail.size() > 1 ? tail.find_last_of('\n', tail.size() - 2) : std::string::npos;
          const size_t footerStart = previous == std::string::npos ? 0 : previous + 1;
          const auto footer = tail.substr(footerStart);
          unsigned long long bodyBytes = 0, records = 0;
          long long firstTime = 0, lastTime = 0;
          int known = 0;
          consumed = 0;
          if (sscanf(footer.c_str(),
              "# esp32m-log-end v1 id=%16[0-9a-f] bytes=%llu records=%llu known=%d first=%lld last=%lld\n%n",
              parsedId, &bodyBytes, &records, &known, &firstTime, &lastTime, &consumed) == 6 &&
              consumed == static_cast<int>(footer.size()) && id == parsedId &&
              bodyBytes == segment.size - footer.size() && records > 0 && known == 1 &&
              firstTime > 0 && lastTime >= firstTime) {
            segment.timeKnown = true;
            segment.firstTime = firstTime;
            segment.lastTime = lastTime;
          }
          _files.push_back(segment);
          std::sort(_files.begin(), _files.end(), [](const Segment& a, const Segment& b) { return a.id < b.id; });
          // Bound catalog memory even if a previous build left excess files.
          if (_files.size() > MaxSegments) {
            if (unlink(path(_files.front().id).c_str()) != 0) {
              result = fail(ioError("prune excess recovered log")); break;
            }
            reclaimed += _files.front().size;
            _files.erase(_files.begin());
          }
        }
        if (closedir(directory) != 0 && result) result = fail(ioError("close log directory"));
        if (!result) return result;
        auto afterRecovery = available;
        afterRecovery.used -= std::min(afterRecovery.used, reclaimed);
        return maintenance(nowMs, afterRecovery);
      }

      Result Engine::readFile(const Segment& segment, size_t offset, size_t limit,
                              Read& out) {
        out = {};
        if (!validId(segment.id) || !limit || limit > MaxRead || offset > segment.size ||
            offset > static_cast<size_t>(std::numeric_limits<long>::max()))
          return fail("invalid log read range");
        const size_t length = std::min(limit, segment.size - offset);
        FILE* reader = fopen(path(segment.id).c_str(), "rb");
        if (!reader) return fail(ioError("open log reader"));
        out.text.resize(length);
        bool ok = fseek(reader, static_cast<long>(offset), SEEK_SET) == 0;
        if (ok && length) ok = fread(&out.text[0], 1, length, reader) == length;
        ok = !ferror(reader) && ok;
        const auto error = ok ? std::string() : ioError("read log");
        ok = fclose(reader) == 0 && ok;
        if (!ok) { out = {}; return fail(error.empty() ? ioError("close log reader") : error); }
        out.offset = offset;
        out.nextOffset = offset + length;
        out.size = segment.size;
        out.eof = out.nextOffset == segment.size;
        return {};
      }
      Result Engine::read(const std::string& id, size_t offset, size_t limit, Read& out) {
        auto file = find(id);
        if (!file) return fail("stale or invalid log ID");
        auto result = flush();
        return result ? readFile(*file, offset, limit, out) : result;
      }
      Result Engine::snapshot(const std::string& id, Snapshot& out) {
        out = {};
        if (!_snapshots.empty()) return fail("log reader busy");
        auto result = flush();
        if (!result) return result;
        if (!id.empty()) {
          auto file = find(id);
          if (!file) return fail("stale or invalid log ID");
          out.files.push_back(*file);
        } else out.files = _files;
        out.token = _nextSnapshot++;
        if (!out.token) out.token = _nextSnapshot++;
        _snapshots.emplace(out.token, out);
        return {};
      }
      Result Engine::readSnapshot(uint64_t token, size_t fileIndex, size_t offset,
                                  size_t limit, Read& out) {
        auto found = _snapshots.find(token);
        if (found == _snapshots.end() || fileIndex >= found->second.files.size())
          return fail("stale log snapshot");
        return readFile(found->second.files[fileIndex], offset, limit, out);
      }
      void Engine::releaseSnapshot(uint64_t token) { _snapshots.erase(token); }
    }  // namespace storage
  }  // namespace log
}  // namespace esp32m
