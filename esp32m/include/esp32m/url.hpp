#pragma once
#include <ArduinoJson.h>
#include <algorithm>
#include <map>
#include <string>

#include "esp32m/logging.hpp"

// based on https://github.com/homer6/url

using namespace std;

namespace esp32m {

  class Url {
   public:
    string scheme;
    string username;
    string password;
    string host;
    string port;
    string path;
    string query;
    string fragment;

    Url(const Url& url, const Url& base) {
      if (base.isAbsolute()) {
        scheme = base.scheme;
        username = base.username;
        password = base.password;
        host = base.host;
        port = base.port;
        path = joinPath(base.path, url.path);
        query = url.query;
        fragment = url.fragment;
      } else
        *this = url;
    }

    Url(const char* url);

    Url(const char* url, const Url& base) : Url(Url(url), base) {}

    Url(const char* url, const char* base) : Url(Url(url), Url(base)) {}

    bool isRelative() const {
      return scheme.empty() && host.empty() && !path.empty();
    }

    bool isAbsolute() const {
      return !scheme.empty() && !host.empty();
    }

    bool empty() const {
      return scheme.empty() && username.empty() && host.empty() &&
             path.empty() && query.empty() && fragment.empty();
    }

    void clear() {
      scheme.clear();
      username.clear();
      password.clear();
      host.clear();
      port.clear();
      path.clear();
      query.clear();
      fragment.clear();
    }

    uint16_t getPortNumber() const {
      if (port.size())
        return strtoul(port.c_str(), nullptr, 10);
      return 0;
    }

    string getPath() const {
      string t;
      if (unescapePath(path, t))
        return t;
      return "";
    }

    string getUserInfo() {
      string result = username;
      if (result.size())
        result += ':';
      if (password.size())
        result += password;
      return result;
    }

    string getAuthority() {
      string result = getUserInfo();
      if (result.size())
        result += '@';
      if (host.size())
        result += host;
      if (port.size())
        result += ':' + port;
      return result;
    }

    string getFullPath() const  // path + query + fragment
    {
      string result;
      string t = getPath();
      if (t.size())
        result += t;
      t = query;
      if (t.size())
        result += "?" + t;
      t = fragment;
      if (t.size())
        result += "#" + t;
      return result;
    }

    string toString() {
      string result;
      if (scheme.size())
        result += scheme + "://";
      result += getAuthority();
      result += getFullPath();
      return result;
    }

    friend bool operator==(const Url& a, const Url& b) {
      return a.scheme == b.scheme && a.username == b.username &&
             a.password == b.password && a.host == b.host && a.port == b.port &&
             a.path == b.path && a.query == b.query && a.fragment == b.fragment;
    }

    friend bool operator!=(const Url& a, const Url& b) {
      return !(a == b);
    }

    friend bool operator<(const Url& a, const Url& b) {
      if (a.scheme < b.scheme)
        return true;
      if (b.scheme < a.scheme)
        return false;

      if (a.username < b.username)
        return true;
      if (b.username < a.username)
        return false;

      if (a.password < b.password)
        return true;
      if (b.password < a.password)
        return false;

      if (a.host < b.host)
        return true;
      if (b.host < a.host)
        return false;

      if (a.port < b.port)
        return true;
      if (b.port < a.port)
        return false;

      if (a.path < b.path)
        return true;
      if (b.path < a.path)
        return false;

      if (a.query < b.query)
        return true;
      if (b.query < a.query)
        return false;

      return a.fragment < b.fragment;
    }

   protected:
    static string joinPath(const string& base, const string& path) {
      if (base.size() && path.size()) {
        if (path[0] == '/')
          return path;
        string result = base;
        if (result[result.size() - 1] != '/')
          result += '/';
        result += path;
        return result;
      }
      if (base.size())
        return base;
      return path;
    }

    static bool unescapePath(const string& in, string& out) {
      out.clear();
      out.reserve(in.size());

      for (size_t i = 0; i < in.size(); ++i) {
        switch (in[i]) {
          case '%':

            if (i + 3 <= in.size()) {
              unsigned int value = 0;

              for (size_t j = i + 1; j < i + 3; ++j) {
                switch (in[j]) {
                  case '0':
                  case '1':
                  case '2':
                  case '3':
                  case '4':
                  case '5':
                  case '6':
                  case '7':
                  case '8':
                  case '9':
                    value += in[j] - '0';
                    break;

                  case 'a':
                  case 'b':
                  case 'c':
                  case 'd':
                  case 'e':
                  case 'f':
                    value += in[j] - 'a' + 10;
                    break;

                  case 'A':
                  case 'B':
                  case 'C':
                  case 'D':
                  case 'E':
                  case 'F':
                    value += in[j] - 'A' + 10;
                    break;

                  default:
                    return false;
                }

                if (j == i + 1)
                  value <<= 4;
              }

              out += static_cast<char>(value);
              i += 2;

            } else
              return false;

            break;

          case '-':
          case '_':
          case '.':
          case '!':
          case '~':
          case '*':
          case '\'':
          case '(':
          case ')':
          case ':':
          case '@':
          case '&':
          case '=':
          case '+':
          case '$':
          case ',':
          case '/':
          case ';':
            out += in[i];
            break;

          default:
            if (!isalnum(in[i]))
              return false;
            out += in[i];
            break;
        }
      }

      return true;
    }
  };

  class UrlParser {
   public:
    UrlParser(const string& source) : _source(source) {}

    bool parse(Url& target) {
      target.clear();

      _parsing = _source;
      left_position = 0;

      bool authority_present = false;
      string authority;

      if (existsForward("://")) {
        // scheme
        captureUpTo(":", target.scheme);
        transform(target.scheme.begin(), target.scheme.end(),
                  target.scheme.begin(),
                  [](string_view::value_type c) { return tolower(c); });
        left_position += target.scheme.size() + 1;

        // authority
        if (moveBefore("//")) {
          authority_present = true;
          left_position += 2;
        }
        if (authority_present)
          captureUpTo("/", authority);

        moveBefore("/");
      }

      if (existsForward("?")) {
        captureUpTo("?", target.path);
        moveBefore("?");
        left_position++;

        if (existsForward("#")) {
          captureUpTo("#", target.query);
          moveBefore("#");
          left_position++;
          captureUpTo("#", target.fragment);
        } else
          captureUpTo("#", target.query);
      } else {
        // no query
        if (existsForward("#")) {
          captureUpTo("#", target.path);
          moveBefore("#");
          left_position++;
          captureUpTo("#", target.fragment);
        } else
          captureUpTo("#", target.path);
      }

      // parse authority

      // reset target
      _parsing = authority;
      left_position = 0;

      string user;

      if (existsForward("@")) {
        captureUpTo("@", user);
        moveBefore("@");
        left_position++;
      }

      // detect ipv6
      if (existsForward("[")) {
        left_position++;
        captureUpTo("]", target.host);
        left_position++;
      } else {
        if (existsForward(":")) {
          captureUpTo(":", target.host);
          moveBefore(":");
          left_position++;
          captureUpTo("#", target.port);
        } else
          // no port
          captureUpTo(":", target.host);
      }

      // parse user_info

      // reset target
      _parsing = user;
      left_position = 0;

      if (existsForward(":")) {
        captureUpTo(":", target.username);
        moveBefore(":");
        left_position++;
        captureUpTo("#", target.password);
      } else {
        // no password
        captureUpTo(":", target.username);
      }
      return true;
    }

   private:
    string _source;
    size_t left_position = 0;
    string_view _parsing;
    void captureUpTo(const char* right_delimiter, string& target) {
      if (left_position >= _parsing.size()) {
        target.clear();
        return;
      }
      auto rp = _parsing.find(right_delimiter, left_position);
      auto size = rp == string::npos ? _parsing.size() - left_position
                                     : rp - left_position;
      /*printf("captureUpTo: lp=%d, size=%d, parsing %s\n", left_position, size,
             _parsing.data());*/
      target = _parsing.substr(left_position, size);
    }
    bool moveBefore(const char* right_delimiter) {
      size_t position =
          this->_parsing.find(right_delimiter, this->left_position);

      if (position != string::npos) {
        this->left_position = position;
        return true;
      }

      return false;
    }

    bool existsForward(const char* right_delimiter) {
      size_t position = _parsing.find(right_delimiter, this->left_position);
      /*printf("existsForward %s: lp=%d, size=%d, parsing %s\n",
         right_delimiter, left_position, position, _parsing.data());*/

      return position != string::npos;
    }
  };

  /*namespace url {

    static bool create(const char* url, Url& target) {
      UrlParser parser(url);
      return parser.parse(target);
    }

    static bool create(const Url* base, const char* url, Url& target) {
      Url parsed;
      UrlParser parser(url);
      auto result = parser.parse(parsed);
      if (result)
        create(base, parsed, target);
      return result;
    }

    static bool create(const char* base, const char* url, Url& target) {
      Url parsed;
      UrlParser parser(base);
      auto result = parser.parse(parsed);
      return result && create(&parsed, url, target);
    }

  }  // namespace url
*/

  namespace json {

    size_t size(const Url& url);
    void to(JsonObject target, const Url& url);
    bool from(JsonVariantConst source, Url& target, bool* changed = nullptr);

  }  // namespace json
}  // namespace esp32m