#pragma once

#include "esp32m/app.hpp"

namespace esp32m {

  namespace debug {
    /*
     * CONFIG_FREERTOS_USE_TRACE_FACILITY=y must be set in sdkconfig to use this
     * class. Additionally, CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y provides
     * CPU usage info
     */
    class Tasks : public AppObject {
     public:
      Tasks(const Tasks &) = delete;
      static Tasks &instance();
      const char *name() const override {
        return "tasks";
      }

     protected:
      JsonDocument *getState(RequestContext &ctx) override;

     private:
      Tasks() {}
    };

    Tasks *useTasks();

  }  // namespace debug

}  // namespace esp32m