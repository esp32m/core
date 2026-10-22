#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <memory>
#include <new>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "esp32m/log/vfs.hpp"
#include "esp32m/net/ota.hpp"

namespace esp32m {
  namespace log {
    Vfs::~Vfs() {
      std::lock_guard<std::mutex> guard(_lock);
      close();
    }

    bool Vfs::close() {
      auto file = _file;
      _file = nullptr;
      return !file || fclose(file) == 0;
    }

    bool Vfs::rotate() {
      if (_name.size() > SIZE_MAX - 5 || _name.size() > INT_MAX - 5)
        return false;
      // A uint8_t file limit needs at most three suffix digits plus dot/NUL.
      const auto size = _name.size() + 5;
      std::unique_ptr<char[]> source(new (std::nothrow) char[size]);
      std::unique_ptr<char[]> target(new (std::nothrow) char[size]);
      if (!source || !target || !close())
        return false;
      for (int i = static_cast<int>(_maxFiles) - 2; i >= 0; --i) {
        const auto written = i ? snprintf(source.get(), size, "%s.%d",
                                          _name.c_str(), i)
                               : snprintf(source.get(), size, "%s",
                                          _name.c_str());
        if (written < 0 || static_cast<size_t>(written) >= size)
          return false;
        struct stat st;
        if (stat(source.get(), &st)) {
          if (errno == ENOENT)
            continue;
          return false;
        }
        const auto targetWritten =
            snprintf(target.get(), size, "%s.%d", _name.c_str(), i + 1);
        if (targetWritten < 0 || static_cast<size_t>(targetWritten) >= size)
          return false;
        if (unlink(target.get()) && errno != ENOENT)
          return false;
        if (rename(source.get(), target.get()))
          return false;
      }
      _file = fopen(_name.c_str(), "a");
      return _file != nullptr;
    }

    bool Vfs::append(const char *message) {
      if (!xPortCanYield())  // called from ISR
        return false;
      if (net::ota::isRunning())
        return false;
      std::lock_guard<std::mutex> guard(_lock);
      if (_name.empty())
        return false;
      if (!_file)
        _file = fopen(_name.c_str(), "a");
      if (!_file)
        return false;
      if (_maxFiles > 1) {
        struct stat st;
        // A failed inspection/rotation must never turn a bounded logger into
        // an unlimited appender. Keep the active file intact and retry later.
        if (stat(_name.c_str(), &st) || (shouldRotate() && !rotate())) {
          close();
          return false;
        }
      }
      if (!message)
        return true;
      const auto length = strlen(message);
      if (length >= INT_MAX ||
          fprintf(_file, "%s\n", message) != static_cast<int>(length + 1) ||
          fflush(_file)) {
        close();
        return false;
      }
      return true;
    }

    bool Vfs::shouldRotate() {
      struct stat st;
      return (stat(_name.c_str(), &st) != 0 || st.st_size > 8192);
    }
  }  // namespace log
}  // namespace esp32m
