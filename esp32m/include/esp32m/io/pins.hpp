#pragma once

#include <esp32m/defs.hpp>

#include <driver/pcnt.h>
#include <esp_bit_defs.h>
#include <esp_err.h>
#include <hal/adc_types.h>
#include <hal/gpio_types.h>
#include <map>
#include <mutex>

namespace esp32m {
  namespace io {
    namespace pin {
      enum class Type { ADC, Pcnt };

      class Impl {
       public:
        virtual ~Impl(){};
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

      class IPcnt : public Impl {
       public:
        Type type() override {
          return Type::Pcnt;
        };
        virtual esp_err_t read(int &value, bool reset = false) = 0;
        virtual esp_err_t setLimits(int16_t min, int16_t max) = 0;
        virtual esp_err_t setMode(pcnt_count_mode_t pos,
                                  pcnt_count_mode_t neg) = 0;
        virtual esp_err_t setFilter(uint16_t filter) = 0;
      };

    }  // namespace pin

    class IPin;

    class IPins {
     public:
      IPins(int pinCount);
      virtual IPin *pin(int num) = 0;
      int pinBase() const {
        return _pinBase;
      }

     protected:
      int _pinBase;
      static int _reservedIds;
      static std::map<int, IPin *> _pins;
      static std::mutex _pinsMutex;
      static esp_err_t add(IPin *pin);
    };

    class IPin {
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
      void reset();
      virtual Features features() = 0;
      virtual esp_err_t setDirection(gpio_mode_t mode) = 0;
      virtual esp_err_t setPull(gpio_pull_mode_t pull) = 0;
      virtual esp_err_t digitalRead(bool &value) = 0;
      virtual esp_err_t digitalWrite(bool value) = 0;

      pin::IADC *adc() {
        return (pin::IADC *)impl(pin::Type::ADC);
      }
      pin::IPcnt *pcnt() {
        return (pin::IPcnt *)impl(pin::Type::Pcnt);
      }

     protected:
      virtual pin::Impl *createImpl(pin::Type type) {
        return nullptr;
      };
      int _id;
      std::mutex _mutex;

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