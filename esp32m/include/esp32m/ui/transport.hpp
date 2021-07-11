#pragma once

#include "esp32m/logging.hpp"

#include <ArduinoJson.h>
#include <esp_err.h>

namespace esp32m {
  class Ui;

  namespace ui {

    class Transport : public log::Loggable {
     public:
      virtual ~Transport() = default;
      virtual esp_err_t wsSend(uint32_t cid, const char *text) = 0;

     protected:
      Ui *_ui = nullptr;
      void incoming(uint32_t cid, void *data, size_t len);
      void incoming(uint32_t cid, DynamicJsonDocument *json);
      void sessionClosed(uint32_t cid);
      virtual void init(Ui *ui) {
        _ui = ui;
      };
      friend class esp32m::Ui;
    };

  }  // namespace ui
}  // namespace esp32m
