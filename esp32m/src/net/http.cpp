#include "esp32m/net/http.hpp"
#include <map>
#include "esp32m/defs.hpp"

namespace esp32m {

  namespace net {
    namespace http {

      static bool shouldRedirect(int status_code) {
        switch (status_code) {
          case HttpStatus_MovedPermanently:
          case HttpStatus_Found:
          case HttpStatus_SeeOther:
          case HttpStatus_TemporaryRedirect:
          case HttpStatus_PermanentRedirect:
            return true;
          default:
            return false;
        }
        return false;
      }

      class Session {
       public:
        esp_http_client_config_t config = {};
        std::multimap<std::string, std::string> headers;
        int64_t contentLength = -1;
        int status = -1;

        Session(UrlResourceRequest* urr);
        ~Session() {
          if (_client) {
            close();
            esp_http_client_cleanup(_client);
            _client = nullptr;
          }
        }
        esp_err_t start() {
          if (!_client)
            _client = esp_http_client_init(&config);
          auto& errors = _request->errors();
          if (!_client)
            return errors.check(ESP_ERR_NO_MEM);
          do {
            ESP_CHECK_RETURN(errors.check(esp_http_client_open(_client, 0)));
            _opened = true;
            contentLength = esp_http_client_fetch_headers(_client);
            status = esp_http_client_get_status_code(_client);
            ESP_CHECK_RETURN(handleStatus());
          } while (_redurecting);
          return ESP_OK;
        }
        Resource* read() {
          Resource* resource = nullptr;
          auto& errors = _request->errors();
          if (contentLength > 0) {
            char* buf = (char*)malloc(
                contentLength + (_request->options.addNullTerminator ? 1 : 0));
            if (buf) {
              int read_len = esp_http_client_read(_client, buf, contentLength);
              if (read_len > 0) {
                logd("%d bytes received from %s", read_len, config.url);
                resource = new Resource(config.url);
                if (_request->options.addNullTerminator)
                  buf[read_len] = 0;
                resource->setData(buf, read_len);
                resource->setMeta(headersToJson());
              } else {
                free(buf);
                errors.check(ESP_FAIL);
              }
            } else
              errors.check(ESP_ERR_NO_MEM);
          }
          return resource;
        }
        esp_err_t handle(esp_http_client_event_t* evt) {
          switch (evt->event_id) {
            case HTTP_EVENT_ON_HEADER:
              headers.insert({std::string(evt->header_key),
                              std::string(evt->header_value)});
              break;
            default:
              break;
          }
          return ESP_OK;
        }
        DynamicJsonDocument* headersToJson() {
          auto count = headers.size();
          if (count == 0)
            return nullptr;
          size_t size = JSON_OBJECT_SIZE(count);
          std::map<std::string, int> keycount;
          for (auto const& kv : headers) {
            size += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(kv.first.size()) +
                    JSON_STRING_SIZE(kv.second.size());
            keycount[kv.first]++;
          }
          auto doc = new DynamicJsonDocument(size);
          auto root = doc->to<JsonObject>();
          for (auto const& kv : headers) {
            if (keycount[kv.first] > 0) {
              auto array = root[kv.first].as<JsonArray>();
              if (!array)
                array = root.createNestedArray(kv.first);
              array.add(kv.second);
            } else
              root[kv.first] = kv.second;
          }
          return doc;
        }

       private:
        bool _opened = false;
        bool _redurecting = false;
        UrlResourceRequest* _request;
        esp_http_client_handle_t _client = nullptr;
        esp_err_t handleStatus() {
          _redurecting = false;
          if ((status / 100) == 2)
            return ESP_OK;
          auto& errors = _request->errors();
          if (shouldRedirect(status)) {
            ESP_CHECK_RETURN(
                errors.check(esp_http_client_set_redirection(_client)));
            clearResponseBuffer();
            _redurecting = true;
            return ESP_OK;
          }
          errors.add("HTTP_STATUS_%d", status);
          return ESP_FAIL;
        }
        void clearResponseBuffer() {
          char buf[256];
          for (;;) {
            int data_read = esp_http_client_read(_client, buf, sizeof(buf));
            if (data_read <= 0)
              return;
          }
        }
        void close() {
          if (_opened) {
            esp_http_client_close(_client);
            _opened = false;
          }
        }
      };

      esp_err_t http_event_handler(esp_http_client_event_t* evt) {
        return ((Session*)evt->user_data)->handle(evt);
      }

      Session::Session(UrlResourceRequest* request) : _request(request) {
        config.event_handler = http_event_handler;
        config.user_data = this;
        config.url = request->url();
      }

      Resource* Client::describe(ResourceRequest& req) {
        UrlResourceRequest* urr;
        if (!check(req, &urr))
          return nullptr;
        Session session(urr);
        session.config.method = HTTP_METHOD_HEAD;
        if (session.start() != ESP_OK)
          return nullptr;
        auto resource = new Resource(session.config.url);
        resource->setMeta(session.headersToJson());
        return resource;
      }

      Resource* Client::obtain(ResourceRequest& req) {
        UrlResourceRequest* urr;
        if (!check(req, &urr))
          return nullptr;
        Session session(urr);
        if (session.start() != ESP_OK)
          return nullptr;
        return session.read();
      };

    }  // namespace http
  }    // namespace net
}  // namespace esp32m