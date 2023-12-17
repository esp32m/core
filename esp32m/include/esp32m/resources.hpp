#pragma once

#include "esp32m/errors.hpp"

namespace esp32m {

  class Resource {
   public:
    Resource(const char* name) : _name(name) {}
    Resource(const Resource&) = delete;
    Resource& operator=(Resource&) = delete;
    ~Resource() {
      setData(nullptr, 0);
    }

    const char* name() {
      return _name.c_str();
    }

    void setData(void* data, size_t size, bool owns = true) {
      if (_ownsData && _data)
        free(_data);
      _data = data;
      _size = size;
      _ownsData = owns && data;
    };
    size_t getData(void** data) const {
      if (data)
        *data = _data;
      return _size;
    }
    bool ownsData() const {
      return _ownsData;
    }
    size_t size() const {
      return _size;
    }
    void* data() const {
      return _data;
    }

    void setMeta(DynamicJsonDocument* meta) {
      _meta.reset(meta);
    }
    JsonVariantConst getMeta() {
      return _meta ? _meta->as<JsonVariantConst>()
                   : json::empty().as<JsonVariantConst>();
    }

   private:
    std::string _name;
    void* _data = nullptr;
    size_t _size = 0;
    bool _ownsData = false;
    std::unique_ptr<DynamicJsonDocument> _meta;
  };

  struct ResourceRequestOptions {
    bool addNullTerminator = false;
    bool bypassCache = false;
  };

  class ResourceRequest {
   public:
    virtual const char* type() = 0;
    ErrorList& errors() {
      return _errors;
    }
    ResourceRequestOptions options = {};

   private:
    ErrorList _errors;
  };

  class UrlResourceRequest : public ResourceRequest {
   public:
    UrlResourceRequest(const char* url) : _url(url) {}
    const char* url() const {
      return _url;
    }
    const char* type() override {
      return TYPE;
    }
    static bool is(ResourceRequest& req, UrlResourceRequest** out) {
      auto result = req.type() == TYPE;
      if (result && out)
        *out = (UrlResourceRequest*)&req;
      return result;
    }

   private:
    const char* _url;
    static constexpr const char* const TYPE = "url";
  };

  class ResourceRequestor {
   public:
    virtual Resource* obtain(ResourceRequest& request) = 0;
  };

}  // namespace esp32m
