#include <esp32m/app.hpp>
#include <esp32m/net/ota.hpp>
#include <esp32m/net/wifi.hpp>
#include <esp32m/net/sntp.hpp>
#include <esp32m/net/interfaces.hpp>
#include <esp32m/net/mqtt.hpp>

#include <esp32m/dev/esp32.hpp>
#include <esp32m/bus/scanner/owb.hpp>
#include <esp32m/bus/scanner/i2c.hpp>
#include <esp32m/bus/scanner/modbus.hpp>
#include <esp32m/bus/modbus.hpp>

#include <esp32m/ui.hpp>
#include <esp32m/ui/httpd.hpp>

#include <dist/ui.hpp>

// All ESP32 manager code is in under esp32m namespace
using namespace esp32m;

extern "C" void app_main() {
  App::Init app;
  net::useWifi();
  net::useOta();
  net::useSntp();
  net::useInterfaces();
  dev::useEsp32();
  net::useMqtt();
  bus::scanner::useOwb();
  bus::scanner::useI2C();
  bus::scanner::useModbus();
  auto &ui = Ui::instance();
  ui.addTransport(ui::Httpd::instance());
  initUi(&ui);
}
