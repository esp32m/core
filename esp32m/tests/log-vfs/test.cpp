#include <cassert>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

bool canYield = true, otaRunning = false;
bool xPortCanYield() { return canYield; }
namespace esp32m::net::ota {
  bool isRunning() { return otaRunning; }
}

int openCount = 0, closeCount = 0;
int failOpen = 0, failStat = 0, failWrite = 0, shortWrite = 0,
    failFlush = 0, failClose = 0, failUnlink = 0, failRename = 0;
bool fault(int& remaining) {
  if (!remaining) return false;
  --remaining;
  errno = EIO;
  return true;
}
FILE* testOpen(const char* name, const char* mode) {
  if (fault(failOpen)) return nullptr;
  // ESP VFS uses POSIX newlines; prevent Windows CRT text translation.
  const auto binaryMode = std::string(mode) + "b";
  auto* file = fopen(name, binaryMode.c_str());
  if (file) ++openCount;
  return file;
}
int testClose(FILE* file) {
  ++closeCount;
  const auto result = fclose(file);
  return fault(failClose) ? EOF : result;
}
int testStat(const char* path, struct stat* value) {
  return fault(failStat) ? -1 : stat(path, value);
}
int testWrite(FILE* file, const char* format, ...) {
  if (fault(failWrite)) return -1;
  if (fault(shortWrite)) return 1;
  va_list args;
  va_start(args, format);
  const auto result = vfprintf(file, format, args);
  va_end(args);
  return result;
}
int testFlush(FILE* file) { return fault(failFlush) ? EOF : fflush(file); }
int testUnlink(const char* path) {
  if (fault(failUnlink)) return -1;
  const auto result = unlink(path);
  if (result && errno != ENOENT)
    std::cerr << "unlink " << path << " errno=" << errno << '\n';
  return result;
}
int testRename(const char* from, const char* to) {
  if (fault(failRename)) return -1;
  const auto result = rename(from, to);
  if (result)
    std::cerr << "rename " << from << " -> " << to << " errno=" << errno << '\n';
  return result;
}

namespace esp32m::log {
  struct LogMessage { const char* value; };
  using LogMessageFormatter = char* (*)(const LogMessage*);
  int formatterCalls = 0;
  char* formatMessage(const LogMessage* message) {
    ++formatterCalls;
    assert(message);
    if (!message->value) return nullptr;
    return strdup(message->value);
  }
  LogMessageFormatter formatter() { return formatMessage; }
  class FormattingAppender {
   public:
    FormattingAppender(LogMessageFormatter formatter = nullptr);
    virtual bool append(const LogMessage* message);
    virtual bool append(const char* message) = 0;
   private:
    LogMessageFormatter _formatter;
  };
}

#define fopen(...) testOpen(__VA_ARGS__)
#define fclose(...) testClose(__VA_ARGS__)
#define stat(...) testStat(__VA_ARGS__)
#define fprintf(...) testWrite(__VA_ARGS__)
#define fflush(...) testFlush(__VA_ARGS__)
#define unlink(...) testUnlink(__VA_ARGS__)
#define rename(...) testRename(__VA_ARGS__)
#include "production.inc"
#undef fopen
#undef fclose
#undef stat
#undef fprintf
#undef fflush
#undef unlink
#undef rename

class Appender : public esp32m::log::Vfs {
 public:
  using Vfs::Vfs;
  using Vfs::append;
};
std::string read(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const std::string& path, const std::string& contents) {
  std::ofstream file(path, std::ios::binary);
  file << contents;
}

int main() {
  namespace fs = std::filesystem;
  {
    Appender empty(nullptr);
    assert(!empty.append("missing filename"));
  }
  {
    std::string name = "owned.log";
    const auto before = closeCount;
    {
      Appender app(name.c_str());
      name.assign(1000, 'x');
      assert(app.append("kept name"));
      assert(read("owned.log") == "kept name\n");
      assert(app.append(static_cast<const char*>(nullptr)));
    }
    assert(closeCount == before + 1);
  }
  {
    Appender app("gates.log");
    const auto before = openCount;
    otaRunning = true;
    assert(!app.append("ota"));
    otaRunning = false;
    canYield = false;
    assert(!app.append("isr"));
    canYield = true;
    assert(openCount == before && !fs::exists("gates.log"));
    failOpen = 1;
    assert(!app.append("open failure"));
    assert(app.append("recovered"));
  }
  for (const auto limit : {0, 1}) {
    const auto name = "unlimited" + std::to_string(limit) + ".log";
    Appender app(name.c_str(), limit);
    assert(app.append(std::string(9000, 'u').c_str()));
    assert(app.append("after threshold"));
    assert(!fs::exists(name + ".1"));
    assert(read(name).size() > 9000);
  }
  {
    const std::string large(8193, 'a');
    Appender app("rolling.log", 3);
    assert(app.append(large.c_str()));
    assert(app.append("next"));
    assert(read("rolling.log") == "next\n");
    assert(read("rolling.log.1") == large + "\n");
    assert(app.append(large.c_str()));
    assert(app.append("third"));
    assert(read("rolling.log") == "third\n");
    assert(read("rolling.log.1") == "next\n" + large + "\n");
    assert(read("rolling.log.2") == large + "\n");
    assert(!fs::exists("rolling.log.3"));
  }
  // Rotation errors must return failure and never append to the full file.
  for (auto* injected : {&failRename, &failUnlink, &failClose}) {
    const auto name = "rotate-failure-" + std::to_string(injected == &failRename ? 1 : injected == &failUnlink ? 2 : 3) + ".log";
    write(name, std::string(8193, 'b'));
    Appender app(name.c_str(), 2);
    *injected = 1;
    assert(!app.append("must not grow"));
    assert(read(name).size() == 8193);
    assert(app.append("recovered"));
    assert(read(name) == "recovered\n");
    assert(read(name + ".1").size() == 8193);
  }
  {
    Appender app("stat-failure.log", 2);
    failStat = 1;
    assert(!app.append("not written"));
    assert(read("stat-failure.log").empty());
    assert(app.append("recovered"));
  }
  for (auto* injected : {&failWrite, &shortWrite, &failFlush}) {
    const auto name = "write-failure-" + std::to_string(injected == &failWrite ? 1 : injected == &shortWrite ? 2 : 3) + ".log";
    Appender app(name.c_str());
    const auto before = closeCount;
    *injected = 1;
    assert(!app.append("cannot confirm"));
    assert(closeCount == before + 1);
    assert(app.append("recovered"));
    assert(read(name).find("recovered\n") != std::string::npos);
  }
  {
    class Readiness : public esp32m::log::FormattingAppender {
     public:
      bool ready = false;
      int probes = 0;
      std::string text;
      bool append(const char* message) override {
        if (!message) { ++probes; return ready; }
        text = message;
        return true;
      }
    } app;
    auto& base = static_cast<esp32m::log::FormattingAppender&>(app);
    auto before = esp32m::log::formatterCalls;
    assert(!base.append(static_cast<const esp32m::log::LogMessage*>(nullptr)));
    app.ready = true;
    assert(base.append(static_cast<const esp32m::log::LogMessage*>(nullptr)));
    assert(app.probes == 2 && esp32m::log::formatterCalls == before);
    esp32m::log::LogMessage message{"formatted"}, omitted{nullptr};
    assert(base.append(&message) && app.text == "formatted");
    assert(base.append(&omitted));
  }
  assert(openCount == closeCount);
  std::cout << "Log Vfs: owned filename, lifecycle, gates, legacy limits, bounded rotation, I/O failures and formatter readiness passed\n";
}
