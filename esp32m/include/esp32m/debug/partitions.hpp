#pragma once

#include "esp32m/app.hpp"

namespace esp32m {

  namespace debug {

    class Partitions : public AppObject {
     public:
      Partitions(const Partitions &) = delete;
      static Partitions &instance();
      const char *name() const override {
        return "partitions";
      }

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;

     private:
      Partitions() {}
    };

    Partitions *usePartitions();

  }  // namespace debug

}  // namespace esp32m