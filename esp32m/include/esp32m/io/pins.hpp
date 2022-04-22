#pragma once

#include "esp32m/defs.hpp"
#include "esp32m/logging.hpp"

#include <driver/ledc.h>
#include <driver/pulse_cnt.h>
#include <esp_bit_defs.h>
#include <esp_err.h>
#include <hal/adc_types.h>
#include <hal/gpio_types.h>
#include <map>
#include <mutex>

namespace esp32m {
  namespace io {
    class IPin;

    namespace pin {
      enum class Type { ADC, DAC, Pcnt, LEDC };

      class Impl {
       public:
        virtual ~Impl() {}
        virtual Type type() = 0;
        virtual bool valid() {
          return true;
        }
      };

      class IADC : public Impl {
       public:
        Type type() override {
          return Type::ADC;
        };
        virtual esp_err_t read(int &value, uint32_t *mv = nullptr) = 0;
        virtual esp_err_t read(float &value, uint32_t *mv = nullptr);
        virtual esp_err_t range(int &min, int &max, uint32_t *mvMin = nullptr,
                                uint32_t *mvMax = nullptr);
        virtual adc_atten_t getAtten() = 0;
        virtual esp_err_t setAtten(adc_atten_t atten = ADC_ATTEN_DB_0) = 0;
        virtual adc_bits_width_t getWidth() = 0;
        virtual esp_err_t setWidth(adc_bits_width_t width) = 0;
      };
      class IDAC : public Impl {
       public:
        Type type() override {
          return Type::DAC;
        };
        virtual esp_err_t write(float value) = 0;
      };

      class IPcnt : public Impl {
       public:
        Type type() override {
          return Type::Pcnt;
        };
        virtual esp_err_t read(int &value, bool reset = false) = 0;
        // virtual esp_err_t setLimits(int16_t min, int16_t max) = 0;
        virtual esp_err_t setMode(pcnt_channel_edge_action_t pos,
                                  pcnt_channel_edge_action_t neg) = 0;
        virtual esp_err_t setFilter(uint16_t filter) = 0;
      };

      class ILEDC : public Impl {
       public:
        Type type() override {
          return Type::LEDC;
        };
        virtual esp_err_t config(
            uint32_t freq_hz, ledc_mode_t mode = LEDC_HIGH_SPEED_MODE,
            ledc_timer_bit_t duty_resolution = LEDC_TIMER_10_BIT,
            ledc_clk_cfg_t clk_cfg = LEDC_AUTO_CLK) = 0;
        virtual esp_err_t setDuty(uint32_t duty) = 0;
      };

      typedef std::function<void(void)> ISR;
    }  // namespace pin

    class IPins {
     public:
      IPin *pin(int num);
      int pinBase() const {
        return _pinBase;
      }
      int id2num(int id);

     protected:
      int _pinBase = 0, _pinCount = 0;
      static int _reservedIds;
      static std::map<int, IPin *> _pins;
      static std::mutex _pinsMutex;
      IPins(){};
      void init(int pinCount);
      virtual IPin *newPin(int id) = 0;
      static esp_err_t add(IPin *pin);
    };

    class IPin : public virtual log::Loggable {
     public:
      enum class Features {
        None = 0,
        PullUp = BIT0,
        PullDown = BIT1,
        DigitalInput = BIT2,
        DigitalOutput = BIT3,
        ADC = BIT4,
        DAC = BIT5,
        PulseCounter = BIT6,
      };
      IPin(int id) : _id(id){};
      IPin(const IPin &) = delete;
      int id() const {
        return _id;
      }
      std::mutex &mutex() {
        return _mutex;
      }
      virtual void reset();
      virtual Features features() = 0;
      virtual esp_err_t setDirection(gpio_mode_t mode) = 0;
      virtual esp_err_t setPull(gpio_pull_mode_t pull) = 0;
      virtual esp_err_t digitalRead(bool &value) = 0;
      virtual esp_err_t digitalWrite(bool value) = 0;
      virtual esp_err_t attach(pin::ISR handler, gpio_int_type_t type) {
        return ESP_ERR_NOT_SUPPORTED;
      }
      virtual esp_err_t detach() {
        return ESP_ERR_NOT_SUPPORTED;
      }

      pin::IADC *adc() {
        return (pin::IADC *)impl(pin::Type::ADC);
      }
      pin::IDAC *dac() {
        return (pin::IDAC *)impl(pin::Type::DAC);
      }
      pin::IPcnt *pcnt() {
        return (pin::IPcnt *)impl(pin::Type::Pcnt);
      }
      pin::ILEDC *ledc() {
        return (pin::ILEDC *)impl(pin::Type::LEDC);
      }

     protected:
      int _id;
      std::mutex _mutex;
      virtual pin::Impl *createImpl(pin::Type type) {
        return nullptr;
      };

     private:
      pin::Impl *_impl = nullptr;
      pin::Impl *impl(pin::Type type);
    };

    ENUM_FLAG_OPERATORS(IPin::Features)

    namespace pin {
      class ITxFinalizer {
       public:
        virtual esp_err_t commit() = 0;
      };

      class Tx {
       public:
        enum Type {
          Read = BIT0,
          Write = BIT1,
        };
        Tx(Type type, esp_err_t *errPtr = nullptr);
        Tx(const Tx &) = delete;
        ~Tx();
        Type type() const {
          return _type;
        }
        const ITxFinalizer *getFinalizer() const {
          return _finalizer;
        }
        esp_err_t setFinalizer(ITxFinalizer *finalizer);
        void setReadPerformed() {
          _readPerformed = true;
        }
        bool getReadPerformed() const {
          return _readPerformed;
        }
        static Tx *current() {
          return _current;
        }

       private:
        std::mutex _mutex;
        Type _type;
        esp_err_t *_errPtr;
        ITxFinalizer *_finalizer = nullptr;
        bool _readPerformed = false;
        static int _nesting;
        static Tx *_current;
      };

    }  // namespace pin
  }    // namespace io

}  // namespace esp32m