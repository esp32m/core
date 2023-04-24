#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "esp32m/fs/vfs.hpp"
#include "esp32m/json.hpp"

namespace esp32m {
  namespace io {

    Vfs &Vfs::instance() {
      static Vfs i;
      return i;
    }

    Vfs &useVfs() {
      return Vfs::instance();
    }

    bool Vfs::handleRequest(Request &req) {
      if (AppObject::handleRequest(req))
        return true;
      if (req.is("readdir")) {
        const char *dir = nullptr;
        auto reqdata = req.data();
        json::from(reqdata["dir"], dir, "/");
        DIR *dp = opendir(dir);
        if (dp) {
          size_t docsize = 0;
          struct dirent *ep;
          while ((ep = readdir(dp)))
            docsize += JSON_ARRAY_SIZE(1) + JSON_ARRAY_SIZE(3) +
                       JSON_STRING_SIZE(strlen(ep->d_name));
          if (docsize) {
            seekdir(dp, 0);
            char *path = nullptr;
            size_t pathBufSuze = 0;
            auto doc = new DynamicJsonDocument(docsize);
            auto root = doc->to<JsonArray>();
            while ((ep = readdir(dp))) {
              auto a = root.createNestedArray();
              a.add(ep->d_type);
              a.add(ep->d_name);
              auto ps = strlen(dir) + 1 + strlen(ep->d_name) + 1;
              if (ps > pathBufSuze) {
                if (path)
                  free(path);
                path = (char *)malloc(ps);
                pathBufSuze = path ? ps : 0;
              }
              if (path) {
                strcpy(path, dir);
                if (path[strlen(dir) - 1] != '/')
                  strcat(path, "/");
                strcat(path, ep->d_name);
                struct stat st;
                if (!stat(path, &st))
                  a.add(st.st_size);
              }
            }
            if (path)
              free(path);
            req.respond(doc->as<JsonVariantConst>(), false);
            delete doc;
          }
          closedir(dp);
        }
      } else if (req.is("readfile")) {
        const char *path = nullptr;
        long int offset = 0;
        size_t length = 0;
        auto reqdata = req.data();
        json::from(reqdata["file"], path);
        json::from(reqdata["offset"], offset);
        json::from(reqdata["length"], length);
        if (path) {
          struct stat st;
          if (stat(path, &st)) {
            req.respond(ESP_ERR_NOT_FOUND);
          } else {
            if (offset > st.st_size)
              offset = st.st_size;
            if (!length || length > st.st_size - offset)
              length = st.st_size - offset;
            if (length) {
              FILE *file = fopen(path, "r");
              if (!file) {
                logW("file %s could not be opened: %d", path, errno);
                req.respond(ESP_ERR_NOT_FOUND);
              } else {
                if (offset)
                  fseek(file, offset, SEEK_SET);
                char *buf = (char *)malloc(length + 1);
                if (buf) {
                  size_t tr = 0;
                  size_t r = 0;
                  while (tr < length) {
                    r = fread(buf + tr, 1, length - tr, file);
                    if (r <= 0)
                      break;
                    tr += r;
                  }
                  buf[tr] = 0;
                  auto doc = new DynamicJsonDocument(JSON_OBJECT_SIZE(1));
                  doc->set((const char *)buf);
                  req.respond(doc->as<JsonVariantConst>(), false);
                  delete doc;
                  free(buf);
                }
                fclose(file);
              }
            }
          }
        }

      } else
        return false;
      if (!req.isResponded())
        req.respond();
      return true;
    }

  }  // namespace io
}  // namespace esp32m