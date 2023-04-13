#pragma once

//#include <driver/adc.h>
#include <hal/gpio_types.h>
#include <driver/dac_oneshot.h>
#include <driver/touch_sensor.h>
#include <math.h>
#include <soc/touch_sensor_channel.h>
#include <mutex>

namespace esp32m {
  namespace io {
    bool gpio2TouchPad(gpio_num_t pin, touch_pad_t &tp);

    class Sampler {
     public:
      Sampler(size_t capacity, uint8_t bytesPerSample);
      ~Sampler();
      int count() const {
        return _count;
      }
      std::mutex &mutex() {
        return _mutex;
      }
      void add(void *sample);
      float at(int index);
      float avg();
      float stdev();
      float kvar();

     private:
      std::mutex _mutex;
      size_t _capacity;
      uint8_t _bps;
      void *_buf;
      int _index = 0, _count = 0;
      float _sum = 0;
    };

  }  // namespace io
}  // namespace esp32m