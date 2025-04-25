#include <ArduinoJson.h>
#include <math.h>
#include <map>
#include "esp32m/device.hpp"

namespace esp32m {
  namespace dev {
    namespace em {

      enum class Indicator {
        Voltage,
        Current,
        Power,
        ApparentPower,
        ReactivePower,
        PowerFactor,
        Angle,
        Frequency,
        ImpActiveEnergy,
        ExpActiveEnergy,
        ImpReactiveEnergy,
        ExpReactiveEnergy,
        TotalActiveEnergy,
        TotalReactiveEnergy,
        MAX
      };

      const char* indicatorName(Indicator i);

      class Reading {
       public:
        Reading(){};
        Reading(const Reading&) = delete;
        ~Reading() {
          clear();
        }
        void set(float value) {
          if (_data && _three)
            clear();
          _three = false;
          if (!_data)
            _data = malloc(sizeof(float));
          *((float*)_data) = value;
        }
        void set(float v1, float v2, float v3) {
          if (_data && !_three)
            clear();
          _three = true;
          if (!_data)
            _data = malloc(sizeof(float) * 3);
          auto arr = (float*)_data;
          arr[0] = v1;
          arr[1] = v2;
          arr[2] = v3;
        }
        bool isThreePhase() const {
          return _three;
        }
        int count() const {
          return _three ? 3 : 1;
        }
        float get(int index) {
          if (!_data || index < 0 || index >= count())
            return NAN;
          return ((float*)_data)[index];
        }

       private:
        void* _data = nullptr;
        bool _three = false;
        void clear() {
          if (!_data)
            return;
          free(_data);
          _data = nullptr;
        }
      };

      class Data {
       public:
        Data() {}
        Data(const Data&) = delete;
        void set(Indicator i, float v) {
          _data[i].set(v);
        }
        void set(Indicator i, float v1, float v2, float v3) {
          _data[i].set(v1, v2, v3);
        }
        void set(Indicator i, float* v, bool triple) {
          if (triple)
            set(i, v[0], v[1], v[2]);
          else
            set(i, *v);
        }
        /*size_t jsonSize() {
          size_t r = 0;
          for (auto& i : _data) {
            const char* name = indicatorName(i.first);
            if (!name)
              continue;
            r += JSON_OBJECT_SIZE(1);
            if (i.second.isThreePhase())
              r += JSON_ARRAY_SIZE(3);
          }
          return r;
        }*/
        void toJson(JsonObject obj) {
          for (auto& i : _data) {
            const char* name = indicatorName(i.first);
            if (!name)
              continue;
            if (i.second.isThreePhase()) {
              auto arr = obj[name].to<JsonArray>();
              for (auto p = 0; p < 3; p++) arr.add(i.second.get(p));
            } else
              obj[name] = i.second.get(0);
          }
        }
        void sensors(Device* dev) {
          char buf[32];
          for (auto& i : _data) {
            const char* name = indicatorName(i.first);
            if (!name)
              continue;
            if (i.second.isThreePhase())
              for (auto p = 0; p < 3; p++) {
                snprintf(buf, sizeof(buf), "%s%d", name, p + 1);
                dev->sensor(buf, i.second.get(p));
              }
            else
              dev->sensor(name, i.second.get(0));
          }
        }

       private:
        std::map<Indicator, Reading> _data;
      };

    }  // namespace em
  }    // namespace dev
}  // namespace esp32m