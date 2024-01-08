#include <esp_console.h>

#include "esp32m/ui/transport.hpp"

namespace esp32m {
  namespace ui {

    class Console : public Transport {
     public:
      virtual ~Console();
      static Console& instance();
      const char* name() const override {
        return "ui-console";
      };

     protected:
      void init(Ui* ui) override;
      esp_err_t wsSend(uint32_t cid, const char* text) override;

     private:
      Console();
      esp_console_repl_t* _repl;
      esp_console_repl_config_t _repl_config;
      friend int requestHandler(int argc, char** argv);
    };

  }  // namespace ui
}  // namespace esp32m