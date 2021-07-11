#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp32m/log/vfs.hpp"

namespace esp32m {
  namespace log {
    bool Vfs::append(const char *message) {
      bool result = false;
      std::lock_guard<std::mutex> guard(_lock);
      if (!_file)
        _file = fopen(_name, "a");
      if (_file && _maxFiles > 1 && shouldRotate()) {
        auto rn = strlen(_name) + 1 + 3 + 1;
        char *a = (char *)malloc(rn), *b = (char *)malloc(rn);
        fclose(_file);
        for (auto i = _maxFiles - 2; i >= 0; i--) {
          if (i)
            sprintf(a, "%s.%d", _name, i);
          else
            strcpy(a, _name);
          struct stat buffer;
          if (stat(a, &buffer) == 0) {
            sprintf(b, "%s.%d", _name, i + 1);
            if (stat(b, &buffer) == 0)
              unlink(b);
            rename(a, b);
          }
        }
        free(a);
        free(b);
        _file = fopen(_name, "a");
      }
      if (_file)
        result = !message || fprintf(_file, "%s\n", message) > 0;
      return result;
    }

    bool Vfs::shouldRotate() {
      struct stat st;
      return (stat(_name, &st) == 0 && st.st_size > 8192);
    }
  }  // namespace log
}  // namespace esp32m
