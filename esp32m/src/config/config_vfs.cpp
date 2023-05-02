#include "esp32m/config/vfs.hpp"
#include "esp32m/json.hpp"

#include <esp_rom_crc.h>
#include <stdio.h>
#include <sys/stat.h>

namespace esp32m {

  const char *ConfigVfs::name() const {
    return "config-vfs";
  }

  size_t ConfigVfs::read(char **bptr, size_t *mu) {
    struct stat st;
    if (stat(_path, &st)) {
      logW("file %s not found", _path);
      return 0;
    }
    if (st.st_size <= 4) {
      logW("file %s is too small (%d bytes)", _path, st.st_size);
      return 0;
    }
    FILE *file = fopen(_path, "r");
    if (!file) {
      logW("file %s could not be opened: %d", _path, errno);
      return 0;
    }
    size_t result = 0;
    char *buf = (char *)malloc(st.st_size + 1);
    size_t tr = 0;
    size_t r = 0;
    while (tr < st.st_size) {
      r = fread(buf + tr, 1, st.st_size - tr, file);
      if (r <= 0)
        break;
      tr += r;
    }
    if (tr == st.st_size) {
      buf[st.st_size] = 0;
      // logD("config: %s", buf + sizeof(size_t));
      *mu = *(size_t *)buf;
      *bptr = buf;
      result = tr;
    } else {
      free(buf);
      if (r == 0 && ferror(file))
        logW("read error: %d", errno);
      else
        logW("only %d bytes of %d read", tr, st.st_size);
    }
    fclose(file);
    return result;
  }

  DynamicJsonDocument *ConfigVfs::read() {
    DynamicJsonDocument *doc = nullptr;
    char *buf;
    size_t mu;
    size_t r = read(&buf, &mu);
    _crc = 0;
    if (r) {
      doc = new DynamicJsonDocument(mu);
      const char *json = buf + sizeof(size_t);
      auto result = deserializeJson(*doc, json);
      if (result != DeserializationError::Ok) {
        logW("%s when parsing '%s', size=%d, mu=%u", result.c_str(), json, r,
             mu);
        delete doc;
        doc = nullptr;
      } else {
        logD("%d bytes loaded", r);
        _crc = esp_rom_crc32_le(mu, (const uint8_t *)json, strlen(json));
      }
      free(buf);
    }
    return doc;
  }

  bool ConfigVfs::check(bool ok, FILE *stream, const char *msg) {
    if (ok)
      return true;
    logW(msg);
    return false;
  }

  void ConfigVfs::write(const DynamicJsonDocument &config) {
    size_t mu = json::measure(config.as<JsonVariantConst>());
    size_t ss;
    char *buf = json::allocSerialize(config, &ss);
    if (!buf) {
      logW("could not serialize config, low memory?");
      return;
    }

    uint32_t crc = esp_rom_crc32_le(mu, (const uint8_t *)buf, ss);

    if (crc != _crc) {
      FILE *file = fopen(_path, "w");
      if (file) {
        if (check(fwrite(&mu, sizeof(mu), 1, file) == 1, file,
                  "error writing config header"))
          if (check(fwrite(buf, 1, ss, file) == ss, file,
                    "error writing config")) {
            // logD("%d bytes saved, mu=%d, data=%s", ss, mu, buf);
            _crc = crc;
          }
        fflush(file);
        fclose(file);
      } else
        logW("could not open %s for writing: %d", _path, errno);
    } /*else
      logD("config not changed, ignoring save request");*/

    free(buf);
  }

  void ConfigVfs::dump() {
    char *buf;
    size_t mu;
    if (read(&buf, &mu)) {
      logD("raw config: [%u] %s", *(size_t *)buf, buf + sizeof(size_t));
      free(buf);
    }
  }

  void ConfigVfs::reset() {
    if (unlink(_path))
      logW("could not remove: %d", errno);
    else
      logD("wiped successfully");
  }

}  // namespace esp32m
