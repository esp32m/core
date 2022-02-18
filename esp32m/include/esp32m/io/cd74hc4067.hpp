#pragma once

#include "esp32m/io/pins.hpp"

namespace esp32m {
  namespace io {
    namespace cd74hc4067 {
      class ADC;
    }

    class CD74HC4067 : public IPins {
     public:
      CD74HC4067(IPin* pinEn, IPin* pinS0, IPin* pinS1, IPin* pinS2,
                 IPin* pinS3, IPin* pinSig);
      CD74HC4067(const CD74HC4067&) = delete;

     protected:
      IPin* newPin(int id) override;

     private:
      IPin *_en, *_s0, *_s1, *_s2, *_s3, *_sig;
      esp_err_t init();
      esp_err_t selectChannel(int c);
      friend class CD74HC4067Pin;
      friend class cd74hc4067::ADC;
    };

    CD74HC4067* useCD74HC4067(IPin* pinEn, IPin* pinS0, IPin* pinS1,
                              IPin* pinS2, IPin* pinS3, IPin* pinSig);

  }  // namespace io
}  // namespace esp32m