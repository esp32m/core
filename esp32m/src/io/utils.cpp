#include "sdkconfig.h"
#include "esp32m/io/utils.hpp"

// #include <soc/adc_channel.h>

namespace esp32m {
  namespace io {
    /*bool gpio2Adc(gpio_num_t pin, int &c1, int &c2) {
      c1 = -1;
      c2 = -1;
      switch (pin) {
        case GPIO_NUM_36:
          c1 = ADC1_GPIO36_CHANNEL;
          return true;
        case GPIO_NUM_37:
          c1 = ADC1_GPIO37_CHANNEL;
          return true;
        case GPIO_NUM_38:
          c1 = ADC1_GPIO38_CHANNEL;
          return true;
        case GPIO_NUM_39:
          c1 = ADC1_GPIO39_CHANNEL;
          return true;
        case GPIO_NUM_32:
          c1 = ADC1_GPIO32_CHANNEL;
          return true;
        case GPIO_NUM_33:
          c1 = ADC1_GPIO33_CHANNEL;
          return true;
        case GPIO_NUM_34:
          c1 = ADC1_GPIO34_CHANNEL;
          return true;
        case GPIO_NUM_35:
          c1 = ADC1_GPIO35_CHANNEL;
          return true;
        case GPIO_NUM_4:
          c2 = ADC2_GPIO4_CHANNEL;
          return true;
        case GPIO_NUM_0:
          c2 = ADC2_GPIO0_CHANNEL;
          return true;
        case GPIO_NUM_2:
          c2 = ADC2_GPIO2_CHANNEL;
          return true;
        case GPIO_NUM_15:
          c2 = ADC2_GPIO15_CHANNEL;
          return true;
        case GPIO_NUM_13:
          c2 = ADC2_GPIO13_CHANNEL;
          return true;
        case GPIO_NUM_12:
          c2 = ADC2_GPIO12_CHANNEL;
          return true;
        case GPIO_NUM_14:
          c2 = ADC2_GPIO14_CHANNEL;
          return true;
        case GPIO_NUM_27:
          c2 = ADC2_GPIO27_CHANNEL;
          return true;
        case GPIO_NUM_25:
          c2 = ADC2_GPIO25_CHANNEL;
          return true;
        case GPIO_NUM_26:
          c2 = ADC2_GPIO26_CHANNEL;
          return true;
        default:
          return false;
      }
    }

    bool gpio2Dac(gpio_num_t pin, dac_channel_t &ch) {
      switch (pin) {
        case GPIO_NUM_25:
          ch = DAC_GPIO25_CHANNEL;
          return true;
        case GPIO_NUM_26:
          ch = DAC_GPIO26_CHANNEL;
          return true;
        default:
          return false;
      }
    }*/

    bool gpio2TouchPad(gpio_num_t pin, touch_pad_t &tp) {
      switch (pin) {
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
        case GPIO_NUM_1:
          tp = TOUCH_PAD_GPIO1_CHANNEL;
          return true;
        case GPIO_NUM_2:
          tp = TOUCH_PAD_GPIO2_CHANNEL;
          return true;
        case GPIO_NUM_3:
          tp = TOUCH_PAD_GPIO3_CHANNEL;
          return true;
        case GPIO_NUM_4:
          tp = TOUCH_PAD_GPIO4_CHANNEL;
          return true;
        case GPIO_NUM_5:
          tp = TOUCH_PAD_GPIO5_CHANNEL;
          return true;
        case GPIO_NUM_6:
          tp = TOUCH_PAD_GPIO6_CHANNEL;
          return true;
        case GPIO_NUM_7:
          tp = TOUCH_PAD_GPIO7_CHANNEL;
          return true;
        case GPIO_NUM_8:
          tp = TOUCH_PAD_GPIO8_CHANNEL;
          return true;
        case GPIO_NUM_9:
          tp = TOUCH_PAD_GPIO9_CHANNEL;
          return true;
        case GPIO_NUM_10:
          tp = TOUCH_PAD_GPIO10_CHANNEL;
          return true;
        case GPIO_NUM_11:
          tp = TOUCH_PAD_GPIO11_CHANNEL;
          return true;
        case GPIO_NUM_12:
          tp = TOUCH_PAD_GPIO12_CHANNEL;
          return true;
        case GPIO_NUM_13:
          tp = TOUCH_PAD_GPIO13_CHANNEL;
          return true;
        case GPIO_NUM_14:
          tp = TOUCH_PAD_GPIO14_CHANNEL;
          return true;
#else
        case GPIO_NUM_4:
          tp = TOUCH_PAD_GPIO4_CHANNEL;
          return true;
        case GPIO_NUM_0:
          tp = TOUCH_PAD_GPIO0_CHANNEL;
          return true;
        case GPIO_NUM_2:
          tp = TOUCH_PAD_GPIO2_CHANNEL;
          return true;
        case GPIO_NUM_15:
          tp = TOUCH_PAD_GPIO15_CHANNEL;
          return true;
        case GPIO_NUM_13:
          tp = TOUCH_PAD_GPIO13_CHANNEL;
          return true;
        case GPIO_NUM_12:
          tp = TOUCH_PAD_GPIO12_CHANNEL;
          return true;
        case GPIO_NUM_14:
          tp = TOUCH_PAD_GPIO14_CHANNEL;
          return true;
        case GPIO_NUM_27:
          tp = TOUCH_PAD_GPIO27_CHANNEL;
          return true;
        case GPIO_NUM_33:
          tp = TOUCH_PAD_GPIO33_CHANNEL;
          return true;
        case GPIO_NUM_32:
          tp = TOUCH_PAD_GPIO32_CHANNEL;
          return true;
#endif
        default:
          return false;
      }
    }
    Sampler::Sampler(size_t capacity, uint8_t bytesPerSample)
        : _capacity(capacity), _bps(bytesPerSample) {
      _buf = calloc(capacity, bytesPerSample);
    }
    Sampler::~Sampler() {
      if (_buf)
        free(_buf);
    }
    void Sampler::add(void *sample) {
      std::lock_guard lock(_mutex);
      if (_index >= _capacity)
        _index = 0;
      uint32_t ov = 0;
      uint32_t nv = 0;
      void *ptr = (uint8_t *)_buf + _index * _bps;
      switch (_bps) {
        case 1:
          ov = *((uint8_t *)ptr);
          nv = *((uint8_t *)sample);
          *(uint8_t *)ptr = nv;
          break;
        case 2:
          ov = *((uint16_t *)ptr);
          nv = *((uint16_t *)sample);
          *(uint16_t *)ptr = nv;
          break;
        case 3:
          ov = *((uint32_t *)ptr);
          nv = *((uint32_t *)sample) & 0xFFFFFF;
          *(uint16_t *)ptr = nv;
          break;
        case 4:
          ov = *((uint32_t *)ptr);
          nv = *((uint32_t *)sample);
          *(uint32_t *)ptr = nv;
          break;
      }
      _sum = _sum - ov + nv;
      _index++;
      if (_count < _capacity)
        _count++;
    }

    float Sampler::at(int index) {
      if (index < 0 || index > _count)
        return NAN;
      void *ptr = (uint8_t *)_buf + index * _bps;
      switch (_bps) {
        case 1:
          return *((uint8_t *)ptr);
        case 2:
          return *((uint16_t *)ptr);
        case 3:
          return *((uint32_t *)ptr) & 0xFFFFFF;
        case 4:
          return *((uint32_t *)ptr);
        default:
          return NAN;
      }
    }

    float Sampler::avg() {
      return _sum / _count;
    }

    float Sampler::stdev() {
      float avg = _sum / _count;
      float cum = 0;
      for (int i = 0; i < _count; i++) {
        float d = at(i) - avg;
        cum += d * d;
      }
      return sqrt(cum / (_count - 1));
    }

    float Sampler::kvar() {
      return stdev() / avg();
    }

  }  // namespace io
}  // namespace esp32m