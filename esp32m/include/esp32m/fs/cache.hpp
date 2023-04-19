#pragma once

#include <stdio.h>

#include "esp32m/fs/files.hpp"
#include "esp32m/logging.hpp"
#include "esp32m/resources.hpp"

namespace esp32m {
  namespace fs {

    class CachedResource : public log::Loggable, public ResourceRequestor {
     public:
      CachedResource(const char* localPath) : _localPath(localPath){};
      CachedResource(const char* localPath, ResourceRequestor& requestor)
          : _localPath(localPath) {
        addRequestor(requestor);
      };
      CachedResource(const CachedResource&) = delete;
      CachedResource& operator=(CachedResource&) = delete;

      const char* name() const override {
        return _localPath;
      }
      void addRequestor(ResourceRequestor& requestor) {
        _requestors.push_back(requestor);
      }
      Resource* obtain(ResourceRequest& request) override {
        Resource* res = nullptr;
        if (!request.options.bypassCache) {
          FileResourceRequest frr(_localPath);
          frr.options = request.options;
          res = FileResourceRequestor::instance().obtain(frr);
          request.errors().concat(frr.errors());
        }
        if (!res || res->size() == 0) {
          for (auto requestor : _requestors) {
            res = requestor.get().obtain(request);
            if (res && res->size() > 0) {
              save(*res, request);
              break;
            }
            if (res) {
              delete res;
              res = nullptr;
            }
          }
        }
        return res;
      }

      void clear() {
        if (unlink(_localPath))
          logW("could not remove: %d", errno);
        else
          logD("wiped successfully");
      }

     private:
      const char* _localPath;
      std::vector<std::reference_wrapper<ResourceRequestor> > _requestors;
      void save(Resource& res, ResourceRequest& request) {
        auto& errors = request.errors();
        void* buf;
        auto size = res.getData(&buf);
        FILE* file = fopen(_localPath, "w");
        if (file) {
          if (fwrite(buf, 1, size, file) != size)
            errors.fail("could not write file");
          fflush(file);
          fclose(file);
        } else
          errors.fail("could not open %s for writing: %d", _localPath, errno);
      }
    };

  }  // namespace fs
}  // namespace esp32m
