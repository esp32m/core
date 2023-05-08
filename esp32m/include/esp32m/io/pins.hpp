#pragma once

#include "esp32m/defs.hpp"
#include "esp32m/logging.hpp"

#include <driver/ledc.h>
#include <driver/pulse_cnt.h>
#include <esp_bit_defs.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <hal/adc_types.h>
#include <hal/gpio_types.h>
#include <map>
#include <mutex>

namespace esp32m {
  namespace io {
    class IPin;

    namespace pin {
      enum class Type {
        Invalid = -1,
        Digital = 0,
        ADC,
        DAC,
        PWM,
        Pcnt,
        LEDC,
        MAX
      };
      enum class Flags {
        None = 0,
        PullUp = BIT0,
        PullDown = BIT1,
        DigitalInput = BIT2,
        DigitalOutput = BIT3,
        ADC = BIT4,
        DAC = BIT5,
        PulseCounter = BIT6,
        LEDC = BIT7,
      };
      ENUM_FLAG_OPERATORS(Flags)
      enum class FeatureStatus { NotSupported, Supported, Enabled };

      class Feature {
       public:
        virtual ~Feature() {}
        virtual Type type() = 0;
      };

      class IDigital : public Feature {
       public:
        Type type() override {
          return Type::Digital;
        };
        virtual esp_err_t setDirection(gpio_mode_t mode) = 0;
        virtual esp_err_t setPull(gpio_pull_mode_t pull) = 0;
        virtual esp_err_t read(bool &value) = 0;
        virtual esp_err_t write(bool value) = 0;
        virtual esp_err_t attach(QueueHandle_t queue,
                                 gpio_int_type_t type = GPIO_INTR_ANYEDGE) {
          return ESP_ERR_NOT_SUPPORTED;
        }
        virtual esp_err_t detach() {
          return ESP_ERR_NOT_SUPPORTED;
        }
      };

      class IADC : public Feature {
       public:
        Type type() override {
          return Type::ADC;
        };
        virtual esp_err_t read(int &value, uint32_t *mv = nullptr) = 0;
        virtual esp_err_t read(float &value, uint32_t *mv = nullptr);
        virtual esp_err_t range(int &min, int &max, uint32_t *mvMin = nullptr,
                                uint32_t *mvMax = nullptr) = 0;
        virtual adc_atten_t getAtten() = 0;
        virtual esp_err_t setAtten(adc_atten_t atten = ADC_ATTEN_DB_0) = 0;
        virtual int getWidth() = 0;
        virtual esp_err_t setWidth(adc_bitwidth_t width) = 0;
      };

      class IDAC : public Feature {
       public:
        Type type() override {
          return Type::DAC;
        };
        virtual esp_err_t write(float value) = 0;
      };

      class IPWM : public Feature {
       public:
        Type type() override {
          return Type::PWM;
        };
        virtual esp_err_t setDuty(float value) = 0;
        virtual float getDuty() const = 0;
        virtual esp_err_t setFreq(uint32_t value) = 0;
        virtual uint32_t getFreq() const = 0;
        virtual esp_err_t enable(bool on) = 0;
        virtual bool isEnabled() const = 0;
      };

      class IPcnt : public Feature {
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

      class ILEDC : public Feature {
       public:
        Type type() override {
          return Type::LEDC;
        };
        virtual esp_err_t config(
            uint32_t freq_hz, ledc_mode_t mode = LEDC_BEST_SPEED_MODE,
            ledc_timer_bit_t duty_resolution = LEDC_TIMER_10_BIT,
            ledc_clk_cfg_t clk_cfg = LEDC_AUTO_CLK) = 0;
        virtual esp_err_t setDuty(uint32_t duty) = 0;
      };

      typedef std::function<void(void)> ISR;
    }  // namespace pin

    class IPins : public log::Loggable {
     public:
      IPin *pin(int num);
      int count() const {
        return _count;
      }

     protected:
      int _count = 0;
      std::mutex _mutex;
      std::map<int, IPin *> _pins;
      IPins(){};
      void init(int count);
      virtual IPin *newPin(int id) = 0;
    };

    class IPin : public virtual log::Loggable {
     public:
      IPin(int id) : _id(id){};
      IPin(const IPin &) = delete;
      int id() const {
        return _id;
      }
      std::mutex &mutex() {
        return _mutex;
      }
      virtual void reset();
      virtual pin::Flags flags() = 0;
      virtual pin::FeatureStatus featureStatus(pin::Type type) {
        auto it = _features.find(type);
        if (it != _features.end())
          return pin::FeatureStatus::Enabled;
        auto f = flags();
        switch (type) {
          case pin::Type::Digital:
            if ((f & (pin::Flags::DigitalInput | pin::Flags::DigitalOutput)) !=
                0)
              return pin::FeatureStatus::Supported;
            break;
          case pin::Type::ADC:
            if ((f & pin::Flags::ADC) != 0)
              return pin::FeatureStatus::Supported;
            break;
          case pin::Type::DAC:
            if ((f & pin::Flags::DAC) != 0)
              return pin::FeatureStatus::Supported;
            break;
          case pin::Type::PWM:
            if ((f & pin::Flags::DigitalOutput) != 0)
              return pin::FeatureStatus::Supported;
            break;
          case pin::Type::Pcnt:
            if ((f & pin::Flags::PulseCounter) != 0)
              return pin::FeatureStatus::Supported;
            break;
          case pin::Type::LEDC:
            if ((f & pin::Flags::LEDC) != 0)
              return pin::FeatureStatus::Supported;
            break;
          default:
            break;
        }
        return pin::FeatureStatus::NotSupported;
      };

      pin::Feature *feature(pin::Type type);
      pin::IDigital *digital() {
        return (pin::IDigital *)feature(pin::Type::Digital);
      }
      pin::IADC *adc() {
        return (pin::IADC *)feature(pin::Type::ADC);
      }
      pin::IDAC *dac() {
        return (pin::IDAC *)feature(pin::Type::DAC);
      }
      pin::IPWM *pwm() {
        return (pin::IPWM *)feature(pin::Type::PWM);
      }
      pin::IPcnt *pcnt() {
        return (pin::IPcnt *)feature(pin::Type::Pcnt);
      }
      pin::ILEDC *ledc() {
        return (pin::ILEDC *)feature(pin::Type::LEDC);
      }

     protected:
      int _id;
      std::mutex _mutex;
      virtual esp_err_t createFeature(pin::Type type, pin::Feature **feature);

     private:
      std::map<pin::Type, std::unique_ptr<pin::Feature> > _features;
    };

    namespace pins {
      std::map<std::string, IPins *> getProviders();
      IPin *pin(const char *provider, int id);
    }  // namespace pins

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