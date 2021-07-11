#pragma once

#include "esp32m/logging.hpp"

namespace esp32m {
  namespace log {
    class Mqtt : public FormattingAppender {
     public:
      Mqtt(const Mqtt &) = delete;
      static Mqtt &instance();

     private:
      Mqtt() {}
      char *_topic = nullptr;

     protected:
      virtual bool append(const char *message);
    };
  }  // namespace log
}  // namespace esp32m
