#pragma once

#include "esp32m/app.hpp"

namespace esp32m {

  namespace debug {

    class Tasks : public AppObject {
     public:
      Tasks(const Tasks &) = delete;
      static Tasks &instance();
      const char *name() const override {
        return "tasks";
      }

     protected:
      DynamicJsonDocument *getState(const JsonVariantConst args) override;

     private:
      Tasks() {}
    };

    Tasks *useTasks();

  }  // namespace debug

}  // namespace esp32m