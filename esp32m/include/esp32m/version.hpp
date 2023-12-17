#pragma once

#include <cstdint>
#include <string>

using namespace std;

namespace esp32m {

  class Version {
   public:
    Version() {
      clear();
    }
    Version(const char* str) {
      parse(str);
    }
    int16_t major() const {
      return _numbers[0];
    }
    int16_t minor() const {
      return _numbers[1];
    }
    int16_t patch() const {
      return _numbers[2];
    }
    bool empty() {
      for (int i = 0; i < 3; i++)
        if (_numbers[i] >= 0)
          return false;
      return true;
    }
    string toString() {
      if (empty())
        return "";
      string result;
      int m = 2;
      while (m > 0)
        if (_numbers[m] < 0)
          m--;
        else
          break;

      for (int i = 0; i <= m; i++) {
        auto v = _numbers[i];
        if (v < 0)
          v = 0;
        if (result.size())
          result += ".";
        result += to_string(v);
      }
      if (_rest.size())
        result += "-" + _rest;
      return result;
    }

    friend bool operator==(const Version& a, const Version& b) {
      for (int i = 0; i < 3; i++)
        if (a._numbers[i] != b._numbers[i])
          return false;
      return a._rest == b._rest;
    }

    friend bool operator!=(const Version& a, const Version& b) {
      return !(a == b);
    }

    friend bool operator<(const Version& a, const Version& b) {
      for (int i = 0; i < 3; i++)
        if (a._numbers[i] < b._numbers[i])
          return true;
        else if (a._numbers[i] > b._numbers[i])
          return false;
      return a._rest < b._rest;
    }

    friend bool operator>(const Version& a, const Version& b) {
      for (int i = 0; i < 3; i++)
        if (a._numbers[i] < b._numbers[i])
          return false;
        else if (a._numbers[i] > b._numbers[i])
          return true;
      return a._rest > b._rest;
    }

   private:
    int16_t _numbers[3];
    string _rest;
    void clear() {
      for (int i = 0; i < 3; i++) _numbers[i] = -1;
      _rest.clear();
    }
    void parse(const char* str) {
      clear();
      if (!str)
        return;
      string s(str);
      auto l = s.size();
      if (!l)
        return;

      auto restpos = s.find_first_of("-+");
      auto e = restpos == string::npos ? l : restpos;
      size_t p = 0;
      int ni = 0;
      while (p < e && ni < 3) {
        auto dotpos = s.find('.', p);
        if (dotpos == string::npos)
          dotpos = e;
        auto sub = s.substr(p, dotpos - p);
        auto v = strtoul(sub.c_str(), nullptr, 10);
        if (v > INT16_MAX || (v == 0 && sub != "0"))
          v = -1;
        _numbers[ni++] = v;
        p = dotpos + 1;
      }
      if (restpos != string::npos)
        _rest = s.substr(restpos + 1);
    }
  };

}  // namespace esp32m