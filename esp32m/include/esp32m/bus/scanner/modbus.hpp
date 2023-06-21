#pragma once

#include "esp32m/app.hpp"
#include "esp32m/bus/modbus.hpp"
#include "sdkconfig.h"

namespace esp32m {
  namespace bus {
    namespace scanner {

      class Modbus : public virtual AppObject {
       public:
        Modbus(const Modbus &) = delete;
        static Modbus &instance();
        const char *name() const override {
          return "modbus";
        }

       protected:
        DynamicJsonDocument *getConfig(RequestContext &ctx) override;
        bool setConfig(const JsonVariantConst cfg,
                       DynamicJsonDocument **result) override;
        bool handleRequest(Request &req) override;
        void handleEvent(Event &ev) override;

       private:
        TaskHandle_t _task = nullptr;
        Response *_pendingResponse = nullptr;
        int _startAddr = 1, _endAddr = 247, _addr = 1;
        uint16_t _regs = 0, _regc = 0;
#if SOC_UART_HP_NUM > 2
        uart_port_t _uart = UART_NUM_2;
#else
        uart_port_t _uart = UART_NUM_1;
#endif
        uint32_t _baud = 9600;
        modbus::Command _cmd = modbus::Command::None;
        uart_parity_t _parity = UART_PARITY_DISABLE;
        bool _ascii = false;
        uint8_t _addrs[32];
        void *_data = nullptr;
        Modbus();
        void run();
      };

      Modbus *useModbus();

    }  // namespace scanner
  }    // namespace bus
}  // namespace esp32m
