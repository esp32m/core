#include "esp32m/log/storage.hpp"

#include <assert.h>
#include <errno.h>
#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace esp32m::log::storage;
namespace fs = std::filesystem;
constexpr int64_t Day = 86400000;
const int64_t Now = 1900000000000LL;

// Linker wrappers exercise actual production error handling without adding test
// hooks or replacing the storage engine with a model.
int failWrites = 0, failFlushes = 0, failCloses = 0, failDeletes = 0;
extern "C" {
  size_t __real_fwrite(const void*, size_t, size_t, FILE*);
  int __real_fflush(FILE*);
  int __real_fclose(FILE*);
  int __real_unlink(const char*);
  size_t __wrap_fwrite(const void* bytes, size_t size, size_t count, FILE* file) {
    if (failWrites > 0) {
      --failWrites;
      errno = ENOSPC;
      return __real_fwrite(bytes, size, count ? count - 1 : 0, file);
    }
    return __real_fwrite(bytes, size, count, file);
  }
  int __wrap_fflush(FILE* file) {
    if (failFlushes > 0) { --failFlushes; errno = ENOSPC; return EOF; }
    return __real_fflush(file);
  }
  int __wrap_fclose(FILE* file) {
    const int result = __real_fclose(file);
    if (failCloses > 0) { --failCloses; errno = EIO; return EOF; }
    return result;
  }
  int __wrap_unlink(const char* path) {
    if (failDeletes > 0) { --failDeletes; errno = EACCES; return -1; }
    return __real_unlink(path);
  }
}

Capacity available(Engine& engine, size_t total = 1024 * 1024,
                   size_t other = 4096, size_t reserve = 8192) {
  return {total, engine.status().usedBytes + other, reserve};
}
std::string readAll(Engine& engine, const std::string& id) {
  Read chunk;
  std::string text;
  size_t offset = 0;
  do {
    assert(engine.read(id, offset, 127, chunk));
    text += chunk.text;
    offset = chunk.nextOffset;
  } while (!chunk.eof);
  return text;
}
std::string makeCase(const std::string& root, const char* name) {
  auto directory = root + "/" + name;
  fs::create_directory(directory);
  return directory;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string root = argv[1];
  uint64_t highwater = 0;
  auto ids = [&]() { return ++highwater; };
  {
    const Policy defaults;
    assert(defaults.quotaPercent == 0 && defaults.quotaBytes == 256 * 1024);
    const Policy legacy{65536, 0};
    assert(legacy.quotaPercent == 0);
    assert(Engine::quotaBase({100, 20, 0}, 10) == 90);
    assert(Engine::quotaBase({100, 5, 0}, 10) == 100);
    assert(Engine::quotaBase({100, 1000, 0}, 10) == 0);
    assert(Engine::quotaBudget({0, 0, 1}, {65536, 0, 0}, 0) == 655);
    assert(Engine::quotaBudget({0, 0, 100}, {65536, 0, 0}, 0) == 65536);
    assert(Engine::quotaBudget({0, 0, 50}, {SIZE_MAX, 0, 0}, 0) == SIZE_MAX / 2);
    assert(Engine::quotaBudget({0, 0, 100}, {SIZE_MAX, 0, 0}, 0) == SIZE_MAX);
    assert(Engine::quotaBudget({0, 0, 1}, {SIZE_MAX, 0, 0}, 0) == SIZE_MAX / 100);
    assert(Engine::quotaBudget({0, 0, 101}, {SIZE_MAX, 0, 0}, 0) == 0);
    assert(Engine::quotaBudget(legacy, {SIZE_MAX, 0, 0}, 0) == 65536);
    Engine engine(makeCase(root, "percent-math"), ids);
    assert(engine.configure({0, 0, 50}, {100000, 40000, 45000}));
    assert(engine.status().quotaBaseBytes == 60000);
    assert(engine.status().quotaBytes == 30000);
    assert(engine.status().effectiveQuotaBytes == 15000);
    assert(engine.configure({0, 0, 100}, {SIZE_MAX, 0, 0}));
    assert(engine.status().quotaBytes == SIZE_MAX && engine.status().effectiveQuotaBytes == SIZE_MAX);
    assert(!engine.configure({8192, 0, 101}, {65536, 0, 0}));
    assert(engine.policy().quotaPercent == 100);
    assert(!engine.configure({0, 0, 0}, {65536, 0, 0}));
    assert(engine.policy().quotaPercent == 100);
    std::cout << "percentage basis, reserve cap, validation and overflow-safe math passed\n";
  }
  {
    const auto directory = makeCase(root, "percent-lifecycle");
    Engine engine(directory, ids);
    const size_t total = 4 * 1024 * 1024;
    size_t other = 1024 * 1024;
    const auto policy = Policy{0, 0, 50};
    assert(engine.configure(policy, available(engine, total, other)));
    const auto base = total - other;
    for (int i = 0; i < 1000; ++i) {
      assert(engine.append(std::string(1900, 'p'), Now, "percent", available(engine, total, other)));
      assert(engine.status().quotaBaseBytes == base);
      assert(engine.status().quotaBytes == base / 2);
      assert(engine.status().usedBytes <= base / 2);
    }
    assert(engine.status().usedBytes > 1024 * 1024);
    assert(engine.seal());
    const auto retained = engine.status().usedBytes;
    const auto first = engine.files().front().id;
    const auto actualCapacity = available(engine, total, other);
    engine.reset();
    assert(engine.configure(policy, actualCapacity));
    assert(engine.recover(actualCapacity));
    assert(engine.status().quotaBaseBytes == base && engine.status().quotaBytes == base / 2);
    assert(engine.status().usedBytes == retained && engine.files().front().id == first);
    other = 2 * 1024 * 1024;
    assert(engine.maintenance(Now, available(engine, total, other)));
    assert(engine.status().quotaBaseBytes == total - other);
    assert(engine.status().quotaBytes == (total - other) / 2);
    assert(engine.status().usedBytes <= (total - other) / 2);
    other = 0;
    assert(engine.maintenance(Now, available(engine, total, other)));
    assert(engine.status().quotaBytes == total / 2);
    for (int i = 0; i < 700; ++i)
      assert(engine.append(std::string(1900, 'g'), Now, "percent", available(engine, total, other)));
    assert(engine.status().usedBytes > 1024 * 1024);
    assert(engine.status().usedBytes <= total / 2);
    assert(engine.status().quotaBaseBytes == total);
    std::cout << "50-percent budget stable through own writes/pruning/reboot; external growth/shrink passed\n";
  }
  {
    Engine engine(makeCase(root, "percent-small"), ids);
    assert(engine.configure({0, 0, 1}, {1024 * 1024, 0, 8192}));
    assert(engine.status().quotaBytes == 10485);
    for (int i = 0; i < 8; ++i)
      assert(engine.append(std::string(1900, 's'), Now, "small", available(engine, 1024 * 1024, 0)));
    assert(engine.maintenance(Now, available(engine, 65536, 0)));
    assert(engine.status().quotaBytes == 655 && engine.status().effectiveQuotaBytes == 655);
    assert(engine.status().usedBytes <= 655);
    const auto count = engine.files().size();
    assert(!engine.append("too small", Now, "small", available(engine, 65536, 0)));
    assert(engine.files().size() == count && engine.status().usedBytes <= 655);
    assert(engine.configure({0, 0, 100}, {65536, engine.status().usedBytes, 60000}));
    assert(engine.status().quotaBytes == 65536 && engine.status().effectiveQuotaBytes == 5536);
    assert(!engine.append("reserve prevents capture", Now, "small",
                          {65536, engine.status().usedBytes, 60000}));
    std::cout << "1/100-percent modes accept low budgets, prune ceiling and pause writes passed\n";
  }
  {
    Engine engine(makeCase(root, "large-quota"), ids);
    const size_t total = 8 * 1024 * 1024;
    assert(engine.configure({2 * 1024 * 1024, 0}, available(engine, total)));
    for (int i = 0; i < 1100; ++i)
      assert(engine.append(std::string(1900, 'b'), Now, "large", available(engine, total)));
    assert(engine.status().usedBytes > 1024 * 1024);
    assert(engine.status().usedBytes <= 2 * 1024 * 1024);
    assert(engine.files().size() <= Engine::MaxSegments);
    std::cout << "multi-MiB quota uses capacity within fixed catalog bound passed\n";
  }
  {
    const auto directory = makeCase(root, "rotation");
    Engine engine(directory, ids);
    assert(engine.configure({8192, 0}, available(engine)));
    assert(engine.recover(available(engine)));
    assert(engine.append("first\nsecond", Now, "boot1", available(engine)));
    const auto first = engine.files().front().id;
    assert(readAll(engine, first).find("first\nsecond\n") != std::string::npos);
    for (int i = 0; i < 35; ++i)
      assert(engine.append(std::string(1700, 'a'), Now + i, "boot1", available(engine)));
    assert(engine.status().usedBytes <= 8192);
    assert(engine.files().size() <= Engine::MaxSegments);
    assert(engine.files().front().id != first);
    assert(engine.seal());
    assert(engine.files().back().timeKnown);
    const auto last = engine.files().back().id;
    const auto expected = readAll(engine, last);
    engine.reset();
    assert(engine.recover({1024 * 1024, fs::file_size(directory + "/ml-" + last + ".txt") + 4096, 8192}));
    assert(engine.files().back().id == last);
    assert(engine.files().back().timeKnown);
    assert(readAll(engine, last) == expected);
    Read out;
    assert(!engine.read("../config.json", 0, 64, out));
    assert(!engine.read(last, 0, Engine::MaxRead + 1, out));
    assert(!engine.read(last, SIZE_MAX, 64, out));
    assert(!engine.append(std::string(Engine::MaxRecord + 1, 'x'), Now, "boot1", available(engine)));
    assert(!engine.configure({Engine::MinimumQuota - 1, 0}, available(engine)));
    assert(engine.policy().quotaBytes == 8192);
    std::cout << "rolling byte quota, restart metadata, read validation passed\n";
  }
  {
    Engine engine(makeCase(root, "pins"), ids);
    assert(engine.configure({8192, 0}, available(engine)));
    assert(engine.append(std::string(1600, 'p'), Now, "boot2", available(engine)));
    Snapshot shot;
    assert(engine.snapshot("", shot));
    const auto endpoint = shot.files.front().size;
    Read chunk;
    assert(engine.append("after snapshot", Now + 1, "boot2", available(engine)));
    assert(engine.readSnapshot(shot.token, 0, 0, Engine::MaxRead, chunk));
    assert(chunk.size == endpoint && chunk.text.find("after snapshot") == std::string::npos);
    Snapshot second;
    assert(!engine.snapshot("", second));
    Result result;
    for (int i = 0; i < 15; ++i) {
      result = engine.append(std::string(1600, 'q'), Now + i + 2, "boot2", available(engine));
      if (!result) break;
    }
    assert(!result && result.pressure);
    assert(engine.status().usedBytes <= 8192);
    result = engine.maintenance(Now, {4096, engine.status().usedBytes, 3072});
    assert(!result && result.pressure);
    engine.releaseSnapshot(shot.token);
    assert(engine.maintenance(Now, {4096, engine.status().usedBytes, 3072}));
    assert(engine.status().usedBytes <= 1024);
    assert(!engine.readSnapshot(shot.token, 0, 0, 8, chunk));
    std::cout << "snapshot endpoints, pins, pressure cancellation and reserve passed\n";
  }
  {
    const auto directory = makeCase(root, "count");
    Engine engine(directory, ids);
    assert(engine.configure({4 * 1024 * 1024, 0}, available(engine, 8 * 1024 * 1024)));
    for (int i = 0; i < 40; ++i) {
      assert(engine.append("small segment", Now, "count", available(engine, 8 * 1024 * 1024)));
      assert(engine.seal());
    }
    assert(engine.files().size() == Engine::MaxSegments);
    const auto newest = engine.files().back().id;
    engine.reset();
    // Place extra valid legacy segments in the directory to verify bounded
    // startup catalog rebuilding and quota accounting after reclamation.
    for (int i = 0; i < 8; ++i) {
      char id[17];
      snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(++highwater));
      std::ofstream file(directory + "/ml-" + id + ".txt", std::ios::binary);
      file << "# esp32m-log v1 id=" << id << " boot=count\nrecord\n";
    }
    size_t bytes = 0;
    for (const auto& entry : fs::directory_iterator(directory)) bytes += entry.file_size();
    assert(engine.recover({8 * 1024 * 1024, bytes + 4096, 8192}));
    assert(engine.files().size() == Engine::MaxSegments);
    assert(engine.files().back().id > newest);
    std::cout << "hard segment count and bounded legacy catalog recovery passed\n";
  }
  {
    const auto directory = makeCase(root, "retention");
    Engine engine(directory, ids);
    assert(engine.configure({65536, 1}, available(engine)));
    assert(engine.append("old active", Now - 2 * Day, "clock", available(engine)));
    assert(engine.maintenance(Now, available(engine)));
    assert(engine.files().empty());
    assert(engine.append("unknown", 0, "unknown", available(engine)));
    assert(engine.seal());
    assert(engine.maintenance(Now + 200 * Day, available(engine)));
    assert(engine.files().size() == 1 && !engine.files().front().timeKnown);
    assert(engine.append("recent", Now, "clock", available(engine)));
    assert(engine.seal());
    assert(engine.configure({8192, 1}, available(engine), Now + 2 * Day));
    assert(engine.files().size() == 1 && !engine.files().front().timeKnown);
    std::cout << "quiet active expiry, unknown clock and policy updates passed\n";
  }
  {
    const auto directory = makeCase(root, "recovery");
    Engine engine(directory, ids);
    assert(engine.configure({65536, 1}, available(engine)));
    assert(engine.append("complete record", Now, "boot3", available(engine)));
    const auto id = engine.files().front().id;
    assert(engine.flush());
    engine.reset();
    { std::ofstream file(directory + "/ml-" + id + ".txt", std::ios::app); file << "partial record"; }
    { std::ofstream file(directory + "/ml-ffffffffffffffff.txt"); file << "unrelated data\n"; }
    { std::ofstream file(directory + "/config.json"); file << "configuration"; }
    assert(engine.recover({1024 * 1024, 8192, 8192}, Now + 100 * Day));
    assert(engine.files().size() == 1 && !engine.files().front().timeKnown);
    assert(readAll(engine, id).find("partial record") == std::string::npos);
    assert(fs::exists(directory + "/ml-ffffffffffffffff.txt"));
    assert(fs::exists(directory + "/config.json"));
    assert(engine.append("new boot", Now, "boot4", available(engine)));
    assert(engine.files().back().id != id);
    std::cout << "interrupted recovery, ownership markers and durable IDs passed\n";
  }
  {
    Engine engine(makeCase(root, "reduction"), ids);
    assert(engine.configure({65536, 0}, available(engine)));
    for (int i = 0; i < 10; ++i)
      assert(engine.append(std::string(1900, 's'), Now, "boot5", available(engine)));
    assert(engine.status().usedBytes > 8192);
    assert(engine.configure({8192, 0}, available(engine)));
    assert(engine.status().usedBytes <= 8192);
    assert(engine.append("continued", Now, "boot5", available(engine)));
    assert(engine.status().usedBytes <= 8192);
    std::cout << "active segment quota reduction and resumed capture passed\n";
  }
  {
    const auto directory = makeCase(root, "faults");
    Engine engine(directory, ids);
    assert(engine.configure({8192, 0}, available(engine)));
    failWrites = 1;
    assert(!engine.append("header fault", Now, "faults", available(engine)));
    assert(engine.files().empty());
    assert(fs::is_empty(directory));
    assert(engine.append("before fault", Now, "faults", available(engine)));
    failWrites = 1;
    assert(!engine.append("short write", Now, "faults", available(engine)));
    assert(!engine.files().back().active && !engine.files().back().timeKnown);
    assert(engine.append("after fault", Now, "faults", available(engine)));
    failFlushes = 1;
    assert(!engine.flush());
    assert(!engine.files().back().active && !engine.files().back().timeKnown);
    assert(engine.append("close fault", Now, "faults", available(engine)));
    failCloses = 1;
    assert(!engine.seal());
    assert(!engine.files().back().active && !engine.files().back().timeKnown);
    failDeletes = 1;
    assert(!engine.maintenance(Now, {0, engine.status().usedBytes, 0}));
    assert(!engine.status().lastError.empty());
    assert(engine.maintenance(Now, {0, engine.status().usedBytes, 0}));
    assert(engine.files().empty());
    std::cout << "short writes, failed header cleanup, flush/close/unlink errors passed\n";
  }
  {
    const auto directory = makeCase(root, "errors");
    Engine engine(directory + "/missing", ids);
    assert(!engine.recover({1024 * 1024, 0, 8192}));
    assert(!engine.append("unavailable", Now, "boot6", {1024 * 1024, 0, 8192}));
    Engine noIds(directory, []() { return uint64_t(0); });
    assert(!noIds.append("no ID", Now, "boot6", {1024 * 1024, 0, 8192}));
    assert(!noIds.status().lastError.empty());
    std::cout << "unavailable storage and ID reservation failures passed\n";
  }
}
