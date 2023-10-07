#include "esp32m/config/vfs.hpp"
#include "esp32m/json.hpp"

#include <esp_rom_crc.h>
#include <stdio.h>
#include <sys/stat.h>

namespace esp32m {
  namespace config {

    const uint32_t MagicVer1 = 0xCFE32001;

    struct __attribute__((packed)) Header {
      uint32_t magic;
      uint32_t fileSize;
      uint32_t jsonSize;
      uint32_t crc;
    };

    class File {
     public:
      File(const File &) = delete;
      uint32_t crc() {
        return _header.crc;
      }
      DynamicJsonDocument *json() {
        return _doc;
      }
      File(const char *path, Header &header, const char *data,
           DynamicJsonDocument *doc)
          : _path(path), _header(header), _data(data), _doc(doc) {}

     private:
      const char *_path;
      Header _header;
      std::string _data;
      DynamicJsonDocument *_doc;
    };

    class Loader : public log::Loggable {
     public:
      Loader(const Loader &) = delete;
      virtual ~Loader() {}
      Loader(const char *path) : _path(path), _name("loader:") {
        _name += path;
      }
      const char *name() const override {
        return _name.c_str();
      }
      File *load() {
        struct stat st;
        if (stat(_path, &st)) {
          logW("file not found");
          return nullptr;
        }
        if (st.st_size <= 4) {
          logW("file is too small (%d bytes)", st.st_size);
          return nullptr;
        }
        FILE *file = fopen(_path, "r");
        if (!file) {
          logW("file could not be opened, errno=%d", errno);
          return nullptr;
        }
        char *buf = (char *)malloc(st.st_size + 1);
        size_t tr = 0;
        size_t r = 0;
        while (tr < st.st_size) {
          r = fread(buf + tr, 1, st.st_size - tr, file);
          if (r <= 0)
            break;
          tr += r;
        }
        const char *data = nullptr;
        Header header = *(Header *)buf;
        if (tr != st.st_size) {
          if (r == 0 && ferror(file))
            logW("read error: %d", errno);
          else
            logW("only %d bytes of %d read", tr, st.st_size);
        } else {
          buf[st.st_size] = 0;
          if (header.magic == MagicVer1) {
            if (header.fileSize != st.st_size)
              logW("file size mismatch: %d!=%d", st.st_size, header.fileSize);
            else {
              data = buf + sizeof(Header);
              auto crc = esp_rom_crc32_le(header.jsonSize,
                                          (const uint8_t *)data, strlen(data));
              if (header.crc != crc) {
                logW("CRC mismatch: 0x%x!=0x%x", crc, header.crc);
                data = nullptr;
              }
            }
          } else  // we may have older version of file, where the first 32 bits
                  // is json document size followed by serialized json string
          {
            auto jsonSize = *(uint32_t *)buf;
            if (jsonSize >
                100 *
                    1024)  // 100Kb of config ought to be enough for everyone :)
              logW("invalid file");
            else {
              data = buf + 4;
              auto crc = esp_rom_crc32_le(jsonSize, (const uint8_t *)data,
                                          strlen(data));
              header = {.magic = 0,
                        .fileSize = (uint32_t)st.st_size,
                        .jsonSize = jsonSize,
                        .crc = crc};
              logW("obsolete file format, will upgrade on the next save");
            }
          }
        }
        fclose(file);
        File *result = nullptr;
        if (data) {
          auto doc = new DynamicJsonDocument(header.jsonSize);
          auto status = deserializeJson(*doc, data);
          if (status != DeserializationError::Ok) {
            logW("%s when parsing '%s', size=%d, mu=%u", status.c_str(), data,
                 header.fileSize, header.jsonSize);
            delete doc;
            doc = nullptr;
          }
          result = new File(_path, header, data, doc);
          logD("loaded %d bytes", header.fileSize);
        }
        free(buf);
        return result;
      }

     private:
      const char *_path;
      std::string _name;
    };

    /*
        size_t Vfs::read(char **bptr, size_t *mu) {
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
        }*/

    DynamicJsonDocument *Vfs::read() {
      auto loader = std::unique_ptr<Loader>(new Loader(_path.c_str()));
      auto file = std::unique_ptr<File>(loader->load());
      if (!file && !_backup.empty()) {
        loader.reset(new Loader(_backup.c_str()));
        file.reset(loader->load());
      }
      DynamicJsonDocument *result = nullptr;
      if (file) {
        result = file->json();
        _crc = file->crc();
      } else
        _crc = 0;

      /*char *buf;
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
      }*/
      return result;
    }

    bool Vfs::check(bool ok, FILE *stream, const char *msg) {
      if (ok)
        return true;
      logW(msg);
      return false;
    }

    void Vfs::write(const DynamicJsonDocument &config) {
      size_t dl =
          measureJson(config) + 1;  // measureJson() excludes null terminator
      char *buf = (char *)malloc(sizeof(Header) + dl);
      if (!buf) {
        logW("could not serialize config, low memory?");
        return;
      }
      char *data = buf + sizeof(Header);
      size_t dataSize = serializeJson(config, data, dl);
      dl--;
      if (dataSize != dl)
        logW("size of serialized JSON is different from expected: %d!=%d",
             dataSize, dl);
      size_t mu = json::measure(config.as<JsonVariantConst>());
      uint32_t crc = esp_rom_crc32_le(mu, (const uint8_t *)data, dataSize);

      if (crc != _crc) {
        Header *header = (Header *)buf;
        header->magic = MagicVer1;
        header->fileSize = sizeof(Header) + dataSize;
        header->jsonSize = mu;
        header->crc = crc;
        if (!_backup.empty()) {
          struct stat st;
          if (!stat(_path.c_str(), &st)) {
            if (!stat(_backup.c_str(), &st))
              unlink(_backup.c_str());
            rename(_path.c_str(), _backup.c_str());
          }
        }
        FILE *file = fopen(_path.c_str(), "w");
        if (file) {
          if (check(fwrite(buf, 1, header->fileSize, file) == header->fileSize,
                    file, "error writing config")) {
            // logD("%d bytes saved, mu=%d, data=%s", ss, mu, buf);
            _crc = crc;
          }
          fflush(file);
          fclose(file);
        } else
          logW("could not open %s for writing: %d", _path, errno);
      }

      /*      size_t mu = json::measure(config.as<JsonVariantConst>());
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
                          // logD("%d bytes saved, mu=%d, data=%s", ss, mu,
         buf); _crc = crc;
                        }
                      fflush(file);
                      fclose(file);
                    } else
                      logW("could not open %s for writing: %d", _path, errno);
                  } else
                    logD("config not changed, ignoring save request");*/

      free(buf);
    }

    void Vfs::dump() {
      auto doc = read();
      if (doc) {
        size_t ss;
        char *buf = json::allocSerialize(*doc, &ss);
        if (buf) {
          logD("raw config: %s", buf);
          free(buf);
        }
      }
      /*      char *buf;
            size_t mu;
            if (read(&buf, &mu)) {
              logD("raw config: [%u] %s", *(size_t *)buf, buf + sizeof(size_t));
              free(buf);
            }*/
    }

    void Vfs::reset() {
      if (unlink(_path.c_str()))
        logW("could not remove: %d", errno);
      else
        logD("wiped successfully");
      if (!_backup.empty())
        unlink(_backup.c_str());
    }
  }  // namespace config

}  // namespace esp32m
