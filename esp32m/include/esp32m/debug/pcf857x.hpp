#pragma once

#include "esp32m/app.hpp"
#include "esp32m/io/pcf857x.hpp"

namespace esp32m {

  namespace debug {

    class Pcf857x : public AppObject {
     public:
      Pcf857x(io::Pcf857x *dev) : _dev(dev){};
      Pcf857x(const Pcf857x &) = delete;
      const char *name() const override {
        return "pcf857x";
      }

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;
      void setState(const JsonVariantConst cfg,
                    DynamicJsonDocument **result) override;

     private:
      io::Pcf857x *_dev;
      void init();
    };

  }  // namespace debug

}  // namespace esp32m