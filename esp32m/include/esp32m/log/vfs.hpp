#pragma once

#include <stdio.h>
#include <mutex>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace log {
    /**
     * Sends output file system (SD, SPIFFS or other)
     */
    class Vfs : public FormattingAppender {
     public:
      Vfs(const char *name, uint8_t maxFiles = 1)
          : _name(name), _maxFiles(maxFiles) {}
      Vfs(const Vfs &) = delete;

     protected:
      virtual bool append(const char *message);
      virtual bool shouldRotate();

     private:
      FILE *_file;
      const char *_name;
      uint8_t _maxFiles;
      std::mutex _lock;
    };
  }  // namespace log
}  // namespace esp32m
