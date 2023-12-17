#pragma once
#include "esp32m/resources.hpp"

#include "esp_http_client.h"

namespace esp32m {

  namespace net {
    namespace http {

      class Client : public ResourceRequestor {
       public:
        Client(const Client&) = delete;
        Client& operator=(Client&) = delete;
        Resource* describe(ResourceRequest& req);
        Resource* obtain(ResourceRequest& req) override;

        static Client& instance() {
          static Client i;
          return i;
        }

       private:
        Client(){};
        bool check(ResourceRequest& req, UrlResourceRequest** urr) {
          auto& errors = req.errors();
          if (!UrlResourceRequest::is(req, urr)) {
            errors.check(ESP_ERR_NOT_SUPPORTED, "request type: %s", req.type());
            return false;
          }
          return true;
        }
      };

    }  // namespace http

  }  // namespace net
}  // namespace esp32m