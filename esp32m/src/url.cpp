#include "esp32m/url.hpp"
#include "esp32m/json.hpp"

namespace esp32m {

  Url::Url(const char* url) {
    UrlParser parser(url);
    parser.parse(*this);
  }

  namespace json {
    size_t size(const Url& url) {
      size_t result = 0;
      auto s = url.scheme.size();
      if (s)
        result += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(s);
      s = url.username.size();
      if (s)
        result += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(s);
      s = url.password.size();
      if (s)
        result += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(s);
      s = url.host.size();
      if (s)
        result += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(s);
      s = url.port.size();
      if (s)
        result += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(s);
      s = url.path.size();
      if (s)
        result += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(s);
      s = url.query.size();
      if (s)
        result += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(s);
      s = url.fragment.size();
      if (s)
        result += JSON_OBJECT_SIZE(1) + JSON_STRING_SIZE(s);
      return result;
    }

    void to(JsonObject target, const Url& url) {
      if (url.scheme.size())
        target["scheme"] = url.scheme;
      if (url.username.size())
        target["username"] = url.username;
      if (url.password.size())
        target["password"] = url.password;
      if (url.host.size())
        target["host"] = url.host;
      if (url.port.size())
        target["port"] = url.port;
      if (url.path.size())
        target["path"] = url.path;
      if (url.query.size())
        target["query"] = url.query;
      if (url.fragment.size())
        target["fragment"] = url.fragment;
    }

    bool from(JsonVariantConst source, Url& target, bool* changed) {
      auto obj = source.as<JsonObjectConst>();
      if (!obj || obj.isNull())
        return false;
      target.clear();
      from(obj["scheme"], target.scheme, changed);
      from(obj["username"], target.username, changed);
      from(obj["password"], target.password, changed);
      from(obj["host"], target.host, changed);
      from(obj["port"], target.port, changed);
      from(obj["path"], target.path, changed);
      from(obj["query"], target.query, changed);
      from(obj["fragment"], target.fragment, changed);
      return true;
    }

  }  // namespace json

}  // namespace esp32m