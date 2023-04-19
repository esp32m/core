#pragma once

#include <stdio.h>
#include <sys/stat.h>

#include "esp32m/logging.hpp"
#include "esp32m/resources.hpp"

namespace esp32m {
  namespace fs {

    class FileResourceRequest : public ResourceRequest {
     public:
      FileResourceRequest(const char* path) : _path(path) {}
      const char* path() const {
        return _path;
      }
      const char* type() override {
        return TYPE;
      }
      static bool is(ResourceRequest& req, FileResourceRequest** out) {
        auto result = req.type() == TYPE;
        if (result && out)
          *out = (FileResourceRequest*)&req;
        return result;
      }

     private:
      const char* _path;
      static constexpr const char* const TYPE = "file";
    };

    class FileResourceRequestor : public ResourceRequestor {
     public:
      FileResourceRequestor(const FileResourceRequestor&) = delete;
      FileResourceRequestor& operator=(FileResourceRequestor&) = delete;
      Resource* obtain(ResourceRequest& req) override {
        auto& errors = req.errors();
        FileResourceRequest* frr;
        if (!FileResourceRequest::is(req, &frr)) {
          errors.check(ESP_ERR_NOT_SUPPORTED, "unsupported request type: %s",
                       req.type());
          return nullptr;
        }
        const char* path = frr->path();
        struct stat st;
        if (stat(path, &st) != 0) {
          errors.check(ESP_ERR_NOT_FOUND, "file %s not found", path);
          return nullptr;
        }
        logd("found %s, size %d", path, st.st_size);
        FILE* file = fopen(path, "r");
        if (!file) {
          errors.check(ESP_ERR_NOT_FOUND, "file %s could not be opened: %d",
                       path, errno);
          return nullptr;
        }

        Resource* resource = nullptr;

        size_t bufsize = st.st_size + (req.options.addNullTerminator ? 1 : 0);
        char* buf = (char*)malloc(bufsize);
        if (buf) {
          size_t tr = 0;
          size_t r = 0;
          while (tr < st.st_size) {
            r = fread(buf + tr, 1, st.st_size - tr, file);
            // logd("fread %d, tr=%d, st_size=%d", r, tr, st.st_size);
            if (r <= 0)
              break;
            tr += r;
          }
          if (tr == st.st_size) {
            if (req.options.addNullTerminator)
              buf[tr] = 0;
            resource = new Resource(path);
            resource->setData(buf, tr);
            // logd("%d bytes read from file %s", tr, path);
          } else {
            free(buf);
            if (r == 0 && ferror(file))
              errors.fail("read error: %d", errno);
            else
              errors.fail("only %d bytes of %d read", tr, st.st_size);
          }
        } else
          errors.check(ESP_ERR_NO_MEM, "failed to allocate %d bytes", bufsize);
        fclose(file);

        return resource;
      };

      static FileResourceRequestor& instance() {
        static FileResourceRequestor i;
        return i;
      }

     private:
      FileResourceRequestor(){};
    };

  }  // namespace fs
}  // namespace esp32m
