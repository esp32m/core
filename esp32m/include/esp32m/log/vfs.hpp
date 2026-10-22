#pragma once

#include <stdio.h>
#include <mutex>
#include <string>

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace log {
    /**
     * Sends output to file system (SD, SPIFFS or other)
     */
    class Vfs : public FormattingAppender {
     public:
      Vfs(const char *name, uint8_t maxFiles = 1)
          : _name(name ? name : ""), _maxFiles(maxFiles) {}
      virtual ~Vfs();
      Vfs(const Vfs &) = delete;

     protected:
      virtual bool append(const char *message);
      virtual bool shouldRotate();

     private:
      FILE *_file = nullptr;
      std::string _name;
      uint8_t _maxFiles;
      std::mutex _lock;
      bool close();
      bool rotate();
    };
  }  // namespace log
}  // namespace esp32m
