#include <ESPAsyncWebServer.h>

#include "esp32m/ui/transport.hpp"

namespace esp32m {
  namespace ui {
    class Aws : public Transport, AsyncWebHandler {
      const char* name() const override {
        return "aws";
      };
      AsyncWebServer _server;
      AsyncWebSocket _ws;
    }
  }  // namespace ui
}  // namespace esp32m