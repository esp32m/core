#include <esp32m/app.hpp>
#include <esp32m/net/ota.hpp>
#include <esp32m/net/wifi.hpp>
#include <esp32m/net/sntp.hpp>
#include <esp32m/net/interfaces.hpp>
// #include <esp32m/net/mqtt.hpp>

#include <esp32m/dev/esp32.hpp>
// #include <esp32m/dev/bme280.hpp>
// #include <esp32m/dev/dsts.hpp>
// #include <esp32m/dev/relay.hpp>

#include <esp32m/ui.hpp>
#include <esp32m/ui/httpd.hpp>

# if CONFIG_ESP32M_UI
  #include <dist/ui.hpp>
# endif

// All ESP32 manager code is in under esp32m namespace
using namespace esp32m;

extern "C" void app_main() {
  // Initialize ESP32 manager. This must be the first line in any esp32m
  // application
  App::Init app;
  net::useWifi();
  // Replace SSID/password with your WiFi credentials if you would like to hard-code AP credentials
  // If SSID/password is not set, ESP32 will start in Access Point mode and you will be able configure
  // WiFi connection via captive portal
  // net::wifi::addAccessPoint("SSID", "password");
  net::useOta();
  net::useSntp();
  net::useInterfaces();
  // Retrieve various pieces of info about ESP32 module to be displayed in the
  // UI.
  dev::useEsp32();

# if CONFIG_ESP32M_WS_API
  # if CONFIG_ESP32M_UI
    // Start embedded HTTP server for UI. 
    initUi(new Ui(new ui::Httpd()));
  # else
    // when using localhost to serve UI, only start ws server on device
    // and run  'yarn start' in ../build/webui to serve web app
    new Ui(new ui::Httpd());
  # endif
# endif

  // Add more devices/modules here:
  // dev::useBme280();
  // dev::useBuzzer(GPIO_NUM_15);
  // dev::useRelay("fan", gpio::pin(GPIO_NUM_32));
  // Send sensor readings to MQTT server. By default it will try to connect to
  // mqtt.lan. Pass different name/IP as you see fit. net::useMqtt();
}
