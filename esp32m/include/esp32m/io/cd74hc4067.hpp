#pragma once

#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    namespace cd74hc4067 {
      class Pin;
      class Digital;
      class ADC;
    }  // namespace cd74hc4067

    class CD74HC4067 : public IPins {
     public:
      CD74HC4067(io::pin::IDigital* pinEn, io::pin::IDigital* pinS0,
                 io::pin::IDigital* pinS1, io::pin::IDigital* pinS2,
                 io::pin::IDigital* pinS3, IPin* pinSig);
      CD74HC4067(const CD74HC4067&) = delete;
      esp_err_t enable(bool en);

     protected:
      IPin* newPin(int id) override;

     private:
      io::pin::IDigital *_en, *_s0, *_s1, *_s2, *_s3;
      IPin* _sig;
      esp_err_t init();
      esp_err_t selectChannel(int c);
      friend class cd74hc4067::Pin;
      friend class cd74hc4067::Digital;
      friend class cd74hc4067::ADC;
    };

    CD74HC4067* useCD74HC4067(IPin* pinEn, IPin* pinS0, IPin* pinS1,
                              IPin* pinS2, IPin* pinS3, IPin* pinSig);

  }  // namespace io
}  // namespace esp32m