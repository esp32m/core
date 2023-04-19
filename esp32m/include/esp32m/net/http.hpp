#include "esp32m/resources.hpp"

#include "esp_http_client.h"

namespace esp32m {

  namespace net {
    namespace http {

      class Client : public ResourceRequestor {
       public:
        Client(const Client&) = delete;
        Client& operator=(Client&) = delete;
        Resource* obtain(ResourceRequest& req) override {
          auto& errors = req.errors();
          UrlResourceRequest* urr;
          if (!UrlResourceRequest::is(req, &urr)) {
            errors.check(ESP_ERR_NOT_SUPPORTED, "unsupported request type: %s",
                         req.type());
            return nullptr;
          }
          esp_http_client_config_t config = {};
          config.url = urr->url();
          Resource* resource = nullptr;
          esp_http_client_handle_t client = esp_http_client_init(&config);
          if (client) {
            esp_err_t err = errors.check(esp_http_client_open(client, 0));
            if (err == ESP_OK) {
              int content_length = esp_http_client_fetch_headers(client);
              if (content_length > 0) {
                char* buf = (char*)malloc(
                    content_length + (req.options.addNullTerminator ? 1 : 0));
                if (buf) {
                  int read_len =
                      esp_http_client_read(client, buf, content_length);
                  if (read_len > 0) {
                    logd("%d bytes received from %s", read_len, config.url);
                    resource = new Resource(config.url);
                    if (req.options.addNullTerminator)
                      buf[read_len] = 0;
                    resource->setData(buf, read_len);
                  } else {
                    free(buf);
                    errors.check(ESP_FAIL);
                  }
                } else
                  errors.check(ESP_ERR_NO_MEM);
              }
              esp_http_client_close(client);
            }
            esp_http_client_cleanup(client);
          }
          return resource;
        };

        static Client& instance() {
          static Client i;
          return i;
        }

       private:
        Client(){};
      };

    }  // namespace http

  }  // namespace net
}  // namespace esp32m