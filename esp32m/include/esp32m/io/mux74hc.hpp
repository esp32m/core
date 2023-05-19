#pragma once

#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    namespace mux74hc {
      class Pin;
      class Digital;
      class ADC;
    }  // namespace mux74hc

    class Mux74hc : public IPins {
     public:
      Mux74hc(io::pin::IDigital* pinEn, io::pin::IDigital* pinS0,
              io::pin::IDigital* pinS1, io::pin::IDigital* pinS2,
              io::pin::IDigital* pinS3, IPin* pinSig);
      Mux74hc(const Mux74hc&) = delete;
      const char* name() const override {
        return "mux74HC";
      }
      esp_err_t enable(bool en);

     protected:
      IPin* newPin(int id) override;

     private:
      io::pin::IDigital *_en, *_s0, *_s1, *_s2, *_s3;
      IPin* _sig;
      esp_err_t init();
      esp_err_t selectChannel(int c);
      friend class mux74hc::Pin;
      friend class mux74hc::Digital;
      friend class mux74hc::ADC;
    };

    Mux74hc* useMux74hc(IPin* pinEn, IPin* pinS0, IPin* pinS1, IPin* pinS2,
                        IPin* pinS3, IPin* pinSig);

  }  // namespace io
}  // namespace esp32m