#pragma once

#include <ArduinoJson.h>
#include <memory>

#include "esp32m/logging.hpp"

namespace esp32m {

  class ConfigStore : public log::Loggable {
   protected:
    virtual void write(const DynamicJsonDocument& config) = 0;
    virtual DynamicJsonDocument* read() = 0;
    virtual void reset() = 0;
    friend class Config;
  };

}  // namespace esp32m
